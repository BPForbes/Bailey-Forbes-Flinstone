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

// Files go only to the named user's sessions, never to everyone.
const carolSock = new MockSocket();
const carol = attachSocket(room, carolSock);
carol.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, "carol"));
for (const s of [aliceSock, bobSock, carolSock]) s.inbox = [];

const { encodeFileOffer, decodeFileOffer } = require("../tools/browser-lab/session-relay-room.js");
const note = new TextEncoder().encode("hi bob");
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, encodeFileOffer("bob", "note.txt", note)));
const delivered = takeOpcode(bobSock, OP.FL_NET_SESSION_OP_FILE_OFFER);
assert.strictEqual(delivered.length, 1);
const got = decodeFileOffer(delivered[0].payload);
assert.strictEqual(got.who, "alice", "the delivered offer names the sender");
assert.strictEqual(got.name, "note.txt");
assert.deepStrictEqual(Array.from(got.bytes), Array.from(note));
assert.strictEqual(takeOpcode(carolSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0, "not broadcast");
const sent = takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_FILE_STATUS);
assert.strictEqual(sent.length, 1);
assert.strictEqual(payloadText(sent[0].payload), "sent note.txt to bob (6 bytes)");

// Nobody by that name: the sender is told, and nothing is delivered.
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, encodeFileOffer("dave", "note.txt", note)));
assert.strictEqual(payloadText(takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_FILE_STATUS)[0].payload),
                   "file not sent: dave is not connected");
assert.strictEqual(takeOpcode(bobSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0);
assert.strictEqual(takeOpcode(carolSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0);

// A path, or a payload whose size field does not match, is refused.
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, encodeFileOffer("bob", "../x", note)));
const bad = encodeFileOffer("bob", "note.txt", note);
bad[bad.length - note.length - 1] += 1; // size one larger than the bytes that follow
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, bad));
const refused = takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_FILE_STATUS).map(f => payloadText(f.payload));
assert.deepStrictEqual(refused, ["file refused: malformed offer", "file refused: malformed offer"]);
assert.strictEqual(takeOpcode(bobSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0);

// A user's own file to themself never loops back to the sending session.
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, encodeFileOffer("alice", "self.txt", note)));
assert.strictEqual(takeOpcode(aliceSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0);

carol.onClose();
bob.onClose();
alice.onClose();
console.log("test_lab_relay_room: PASS");
