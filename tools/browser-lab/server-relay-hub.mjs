#!/usr/bin/env node
/**
 * Browser-lab session hub — speaks the same P3 session wire as net_server.c.
 * Used only when runtimeMode=browser-hosted (online lab). Local VM/bare-metal
 * shells use kernel/core/net (C/ASM) and never connect here.
 */
import { createRequire } from "node:module";

const require = createRequire(import.meta.url);
const { WebSocketServer } = require("ws");
const { OP, encodeFrame, FrameParser, payloadText, memberIdFromPayload } = require("./session-wire.js");

const MAX_MEMBERS = OP.FL_NET_SESSION_MAX_MEMBERS;
const MAX_MSG = OP.FL_NET_SESSION_MAX_MSG;
const HOST_ID = 1;

function parseArgs(argv) {
  const out = { bind: "127.0.0.1", port: 8767, room: "lab" };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === "--bind") out.bind = argv[++i];
    else if (argv[i] === "--port") out.port = Number(argv[++i]);
    else if (argv[i] === "--room") out.room = argv[++i];
  }
  return out;
}

function renderDisplay(member, members) {
  const peers = members.filter(m => m.principal === member.principal);
  if (peers.length <= 1) return member.principal;
  const idx = peers.indexOf(member) + 1;
  return `${member.principal}{${idx}}`;
}

function encodeMemberList(members) {
  const buf = [];
  for (const m of members) {
    const principal = new TextEncoder().encode(m.principal.slice(0, 255));
    const nick = new TextEncoder().encode((m.nick || "").slice(0, 255));
    if (buf.length + 6 + principal.length + nick.length > MAX_MSG) break;
    buf.push((m.memberId >> 8) & 0xff, m.memberId & 0xff, m.isHost ? 1 : 0, m.disambig || 0);
    buf.push(principal.length);
    buf.push(...principal);
    buf.push(nick.length);
    buf.push(...nick);
  }
  return Uint8Array.from(buf);
}

function recomputeDisambig(members) {
  const counts = new Map();
  for (const m of members) {
    counts.set(m.principal, (counts.get(m.principal) || 0) + 1);
  }
  const seen = new Map();
  for (const m of members) {
    if ((counts.get(m.principal) || 0) <= 1) {
      m.disambig = 0;
      continue;
    }
    const next = (seen.get(m.principal) || 0) + 1;
    seen.set(m.principal, next);
    m.disambig = next;
  }
}

class Room {
  constructor(id) {
    this.id = id;
    this.members = [];
    this.nextId = HOST_ID;
  }
  live() {
    return this.members.filter(m => m.socket.readyState === 1);
  }
  broadcast(frame, exceptId = null) {
    for (const m of this.live()) {
      if (exceptId != null && m.memberId === exceptId) continue;
      m.socket.send(frame, { binary: true });
    }
  }
  sendJoinAnnounce(member) {
    const line = `${renderDisplay(member, this.members)} has joined.`;
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_JOIN_ANNOUNCE, line);
    if (frame) this.broadcast(frame);
  }
  sendLeaveAnnounce(member) {
    const line = `${renderDisplay(member, this.members)} has left.`;
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_LEAVE_ANNOUNCE, line);
    if (frame) this.broadcast(frame);
  }
  sendMemberList() {
    const payload = encodeMemberList(this.members);
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT, payload);
    if (frame) this.broadcast(frame);
  }
  removeMember(member) {
    const display = renderDisplay(member, this.members);
    this.members = this.members.filter(m => m !== member);
    recomputeDisambig(this.members);
    const line = `${display} has left.`;
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_LEAVE_ANNOUNCE, line);
    if (frame) this.broadcast(frame);
    this.sendMemberList();
  }
  relayMsg(from, textBytes) {
    const line = `${renderDisplay(from, this.members)}: ${payloadText(textBytes)}`;
    const body = new TextEncoder().encode(line.slice(0, MAX_MSG));
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_MSG_BROADCAST, body);
    if (frame) this.broadcast(frame, from.memberId);
  }
  acceptHello(socket, principal) {
    if (this.live().length >= MAX_MEMBERS) {
      const err = encodeFrame(OP.FL_NET_SESSION_OP_ERR, "session full");
      if (err) socket.send(err, { binary: true });
      socket.close();
      return null;
    }
    const memberId = this.nextId++;
    if (this.nextId === 0xffff) this.nextId = HOST_ID + 1;
    const isHost = this.members.length === 0;
    const member = { socket, memberId, principal, nick: "", isHost, disambig: 0 };
    this.members.push(member);
    recomputeDisambig(this.members);
    const display = renderDisplay(member, this.members);
    const ackBody = new Uint8Array(2 + new TextEncoder().encode(display).length);
    ackBody[0] = (memberId >> 8) & 0xff;
    ackBody[1] = memberId & 0xff;
    ackBody.set(new TextEncoder().encode(display), 2);
    const ack = encodeFrame(OP.FL_NET_SESSION_OP_HELLO_ACK, ackBody);
    if (ack) socket.send(ack, { binary: true });
    this.sendJoinAnnounce(member);
    this.sendMemberList();
    return member;
  }
  dispatch(member, opcode, payload) {
    if (opcode === OP.FL_NET_SESSION_OP_MSG) {
      if (payload.length === 0 || payload.length > MAX_MSG) return;
      this.relayMsg(member, payload);
      return;
    }
    if (opcode === OP.FL_NET_SESSION_OP_CTRL_LEAVE) {
      this.removeMember(member);
      member.socket.close();
    }
  }
}

const rooms = new Map();

function roomFor(id) {
  if (!rooms.has(id)) rooms.set(id, new Room(id));
  return rooms.get(id);
}

function main() {
  const args = parseArgs(process.argv);
  const wss = new WebSocketServer({ host: args.bind, port: args.port, path: "/ws" });
  wss.on("listening", () => {
    console.log(`Session relay: ws://${args.bind}:${args.port}/ws (room=${args.room})`);
  });
  wss.on("connection", (socket, req) => {
    const url = new URL(req.url || "/ws", "http://localhost");
    const roomId = url.searchParams.get("room") || args.room;
    const room = roomFor(roomId);
    const parser = new FrameParser();
    let member = null;
    let greeted = false;

    socket.on("message", data => {
      const chunk = data instanceof Buffer ? Uint8Array.from(data) : new Uint8Array(data);
      for (const frame of parser.push(chunk)) {
        if (!greeted) {
          if (frame.opcode !== OP.FL_NET_SESSION_OP_HELLO || frame.payload.length === 0) {
            socket.close();
            return;
          }
          const principal = payloadText(frame.payload).slice(0, OP.FL_NET_SERVER_PRINCIPAL_MAX - 1);
          member = room.acceptHello(socket, principal);
          greeted = member != null;
          continue;
        }
        if (member) room.dispatch(member, frame.opcode, frame.payload);
      }
    });

    socket.on("close", () => {
      if (member) room.removeMember(member);
    });
  });
}

main();
