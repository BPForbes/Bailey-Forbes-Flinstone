"use strict";
const assert = require("assert");
const { OP, encodeFrame, FrameParser, payloadText, Room, attachSocket } = require("../tools/browser-lab/session-relay-room.js");

class MockSocket {
  constructor() {
    this.readyState = 1;
    this.inbox = [];
  }
  send(frame) {
    this.inbox.push(Uint8Array.from(frame));
  }
  close() {
    this.readyState = 3;
  }
}

const room = new Room("lab");
const aliceSock = new MockSocket();
const bobSock = new MockSocket();
const alice = attachSocket(room, aliceSock);
const bob = attachSocket(room, bobSock);

alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, "alice"));
bob.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, "bob"));

function takeOpcode(socket, opcode) {
  const parser = new FrameParser();
  const frames = [];
  for (const chunk of socket.inbox) frames.push(...parser.push(chunk));
  socket.inbox = [];
  return frames.filter(frame => frame.opcode === opcode);
}

assert(takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_HELLO_ACK).length >= 1);
assert(takeOpcode(bobSock, OP.FL_NET_SESSION_OP_HELLO_ACK).length >= 1);
aliceSock.inbox = [];
bobSock.inbox = [];

alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_MSG, "hello from alice"));
const broadcasts = takeOpcode(bobSock, OP.FL_NET_SESSION_OP_MSG_BROADCAST);
assert.strictEqual(broadcasts.length, 1);
assert.strictEqual(payloadText(broadcasts[0].payload), "alice: hello from alice");
assert.strictEqual(takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_MSG_BROADCAST).length, 0);

bob.onClose();
alice.onClose();
console.log("test_lab_relay_room: PASS");
