"use strict";
const { OP, encodeFrame, FrameParser, payloadText } = require("./session-wire.js");

const MAX_MEMBERS = OP.FL_NET_SESSION_MAX_MEMBERS;
const MAX_MSG = OP.FL_NET_SESSION_MAX_MSG;
const HOST_ID = 1;
const PRINCIPAL_MAX = OP.FL_NET_SERVER_PRINCIPAL_MAX;

function sendBinary(socket, frame) {
  if (!frame || !socket || socket.readyState !== 1) return;
  try {
    socket.send(frame);
  } catch (_) { /* ignore */ }
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
  for (const m of members) counts.set(m.principal, (counts.get(m.principal) || 0) + 1);
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
    return this.members.filter(m => m.socket && m.socket.readyState === 1);
  }
  broadcast(frame, exceptId = null) {
    for (const m of this.live()) {
      if (exceptId != null && m.memberId === exceptId) continue;
      sendBinary(m.socket, frame);
    }
  }
  sendJoinAnnounce(member) {
    const line = `${renderDisplay(member, this.members)} has joined.`;
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_JOIN_ANNOUNCE, line);
    if (frame) this.broadcast(frame);
  }
  sendMemberList() {
    const payload = encodeMemberList(this.members);
    const frame = encodeFrame(OP.FL_NET_SESSION_OP_MEMBER_LIST_SNAPSHOT, payload);
    if (frame) this.broadcast(frame);
  }
  removeMember(member) {
    if (!member || !this.members.includes(member)) return;
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
      sendBinary(socket, encodeFrame(OP.FL_NET_SESSION_OP_ERR, "session full"));
      try { socket.close(); } catch (_) { /* ignore */ }
      return null;
    }
    const memberId = this.nextId++;
    if (this.nextId === 0xffff) this.nextId = HOST_ID + 1;
    const isHost = this.members.length === 0;
    const member = { socket, memberId, principal, nick: "", isHost, disambig: 0 };
    this.members.push(member);
    recomputeDisambig(this.members);
    const display = renderDisplay(member, this.members);
    const encoded = new TextEncoder().encode(display);
    const ackBody = new Uint8Array(2 + encoded.length);
    ackBody[0] = (memberId >> 8) & 0xff;
    ackBody[1] = memberId & 0xff;
    ackBody.set(encoded, 2);
    sendBinary(socket, encodeFrame(OP.FL_NET_SESSION_OP_HELLO_ACK, ackBody));
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
      try { member.socket.close(); } catch (_) { /* ignore */ }
    }
  }
}

function messageBytes(data) {
  if (!data) return new Uint8Array(0);
  if (data instanceof Uint8Array) return data;
  if (data instanceof ArrayBuffer) return new Uint8Array(data);
  if (typeof Buffer !== "undefined" && Buffer.isBuffer(data)) return Uint8Array.from(data);
  if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if (typeof data === "string") return new TextEncoder().encode(data);
  return new Uint8Array(0);
}

function attachSocket(room, socket) {
  const parser = new FrameParser();
  let member = null;
  const onMessage = data => {
    for (const frame of parser.push(messageBytes(data))) {
      if (!member) {
        if (frame.opcode !== OP.FL_NET_SESSION_OP_HELLO || frame.payload.length === 0) {
          try { socket.close(); } catch (_) { /* ignore */ }
          return;
        }
        const principal = payloadText(frame.payload).slice(0, PRINCIPAL_MAX - 1);
        member = room.acceptHello(socket, principal);
        continue;
      }
      room.dispatch(member, frame.opcode, frame.payload);
    }
  };
  const onClose = () => {
    if (!member) return;
    room.removeMember(member);
    member = null;
  };
  return { onMessage, onClose };
}

module.exports = {
  Room, attachSocket, renderDisplay, sendBinary, FrameParser, OP, encodeFrame, payloadText,
};
