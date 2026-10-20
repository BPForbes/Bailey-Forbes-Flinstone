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

const { encodeFileOffer, decodeFileOffer, LAB: FILE_LAB } = require("../tools/browser-lab/session-relay-room.js");
const note = new TextEncoder().encode("hi bob");
alice.onMessage(encodeFrame(FILE_LAB.FILE, encodeFileOffer("bob", "note.txt", note)));
const delivered = takeOpcode(bobSock, FILE_LAB.FILE);
assert.strictEqual(delivered.length, 1);
const got = decodeFileOffer(delivered[0].payload);
assert.strictEqual(got.who, "alice", "the delivered offer names the sender");
assert.strictEqual(got.name, "note.txt");
assert.deepStrictEqual(Array.from(got.bytes), Array.from(note));
assert.strictEqual(takeOpcode(carolSock, FILE_LAB.FILE).length, 0, "not broadcast");
const sent = takeOpcode(aliceSock, FILE_LAB.FILE_STATUS);
assert.strictEqual(sent.length, 1);
assert.strictEqual(payloadText(sent[0].payload), "sent note.txt to bob (6 bytes)");

// Nobody by that name: the sender is told, and nothing is delivered.
alice.onMessage(encodeFrame(FILE_LAB.FILE, encodeFileOffer("dave", "note.txt", note)));
assert.strictEqual(payloadText(takeOpcode(aliceSock, FILE_LAB.FILE_STATUS)[0].payload),
                   "file not sent: dave is not connected");
assert.strictEqual(takeOpcode(bobSock, FILE_LAB.FILE).length, 0);
assert.strictEqual(takeOpcode(carolSock, FILE_LAB.FILE).length, 0);

// A path, or a payload whose size field does not match, is refused.
alice.onMessage(encodeFrame(FILE_LAB.FILE, encodeFileOffer("bob", "../x", note)));
const bad = encodeFileOffer("bob", "note.txt", note);
bad[bad.length - note.length - 1] += 1; // size one larger than the bytes that follow
alice.onMessage(encodeFrame(FILE_LAB.FILE, bad));
const refused = takeOpcode(aliceSock, FILE_LAB.FILE_STATUS).map(f => payloadText(f.payload));
assert.deepStrictEqual(refused, ["file refused: malformed offer", "file refused: malformed offer"]);
assert.strictEqual(takeOpcode(bobSock, FILE_LAB.FILE).length, 0);

// Account names match regardless of case, as lab accounts do.
alice.onMessage(encodeFrame(FILE_LAB.FILE, encodeFileOffer("BOB", "note.txt", note)));
assert.strictEqual(takeOpcode(bobSock, FILE_LAB.FILE).length, 1, "BOB reaches bob");
takeOpcode(aliceSock, FILE_LAB.FILE_STATUS);

// The session wire's own FILE_OFFER keeps its native format: the relay never reads it as a
// lab file, and nothing is delivered.
alice.onMessage(encodeFrame(OP.FL_NET_SESSION_OP_FILE_OFFER, encodeFileOffer("bob", "note.txt", note)));
assert.strictEqual(takeOpcode(bobSock, OP.FL_NET_SESSION_OP_FILE_OFFER).length, 0);
assert.strictEqual(takeOpcode(bobSock, FILE_LAB.FILE).length, 0);
assert.strictEqual(takeOpcode(aliceSock, FILE_LAB.FILE_STATUS).length, 0);

// A user's own file to themself never loops back to the sending session.
alice.onMessage(encodeFrame(FILE_LAB.FILE, encodeFileOffer("alice", "self.txt", note)));
assert.strictEqual(takeOpcode(aliceSock, FILE_LAB.FILE).length, 0);

// KeyQuorum frames: a member's go to the host, stamped with the sender; the host's go to the
// one member it names; nobody else reads them.
const { LAB, encodeKqRoute, decodeKqRoute } = require("../tools/browser-lab/session-relay-room.js");
const ids = {};
for (const [name, sock] of [["alice", aliceSock], ["bob", bobSock], ["carol", carolSock]]) sock.inbox = [];
for (const m of room.members) ids[m.principal] = m.memberId;
assert(room.members.find(m => m.principal === "alice").isHost, "alice joined first and hosts");
const kqBytes = new Uint8Array(512).fill(7); // a KQ payload may fill a whole session frame
bob.onMessage(encodeFrame(LAB.KQ, encodeKqRoute(true, 0, 0x3a, kqBytes)));
let atHost = takeOpcode(aliceSock, LAB.KQ).map(f => decodeKqRoute(f.payload));
assert.strictEqual(atHost.length, 1);
assert.strictEqual(atHost[0].toHost, true);
assert.strictEqual(atHost[0].memberId, ids.bob, "the host learns which member sent it");
assert.strictEqual(atHost[0].op, 0x3a);
assert.deepStrictEqual(Array.from(atHost[0].bytes), Array.from(kqBytes));
assert.strictEqual(takeOpcode(carolSock, LAB.KQ).length, 0);

alice.onMessage(encodeFrame(LAB.KQ, encodeKqRoute(false, ids.bob, 0x3b, Uint8Array.of(1, 2, 3))));
const toBob = takeOpcode(bobSock, LAB.KQ).map(f => decodeKqRoute(f.payload));
assert.strictEqual(toBob.length, 1);
assert.strictEqual(toBob[0].memberId, ids.alice);
assert.strictEqual(takeOpcode(carolSock, LAB.KQ).length, 0, "only the member the host names");

// Only the host addresses members.
carol.onMessage(encodeFrame(LAB.KQ, encodeKqRoute(false, ids.bob, 0x3d, Uint8Array.of(9))));
assert.strictEqual(takeOpcode(bobSock, LAB.KQ).length, 0);

// Published keys reach the host under the sender's principal; the host's own are not echoed.
const keys = new Uint8Array(64).fill(5);
bob.onMessage(encodeFrame(LAB.KQ_KEYS, keys));
alice.onMessage(encodeFrame(LAB.KQ_KEYS, keys));
const published = takeOpcode(aliceSock, LAB.KQ_KEYS);
assert.strictEqual(published.length, 1);
const p = published[0].payload;
assert.strictEqual((p[0] << 8) | p[1], ids.bob);
assert.strictEqual(payloadText(p.slice(3, 3 + p[2])), "bob");
assert.deepStrictEqual(Array.from(p.slice(3 + p[2])), Array.from(keys));
bob.onMessage(encodeFrame(LAB.KQ_KEYS, keys.slice(0, 10)));
assert.strictEqual(takeOpcode(aliceSock, LAB.KQ_KEYS).length, 0, "keys are exactly 64 bytes");

// Other opcodes keep the session frame limit; only LAB.KQ carries its four route bytes more.
assert.strictEqual(encodeFrame(OP.FL_NET_SESSION_OP_MSG, new Uint8Array(513)), null);
assert.strictEqual(encodeFrame(LAB.KQ, new Uint8Array(517)), null);

// The server keeps a host: when alice leaves, the earliest member left takes over.
alice.onClose();
assert(room.members.find(m => m.principal === "bob").isHost);
carol.onMessage(encodeFrame(LAB.KQ, encodeKqRoute(true, 0, 0x3c, Uint8Array.of(4))));
atHost = takeOpcode(bobSock, LAB.KQ).map(f => decodeKqRoute(f.payload));
assert.strictEqual(atHost.length, 1);
assert.strictEqual(atHost[0].memberId, ids.carol);

carol.onClose();
bob.onClose();
alice.onClose();
console.log("test_lab_relay_room: PASS");
