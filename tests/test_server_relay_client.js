"use strict";
const assert = require("assert");

globalThis.FlintstoneSessionWire = require("../tools/browser-lab/session-wire.js");
const {
  createRelayClient, shouldTryWebSocket, formatChatLine, wsUrl, healthUrl,
} = require("../tools/browser-lab/server-relay-client.js");

assert.strictEqual(formatChatLine("flinstone", "Hi"), "flinstone: Hi");
assert.strictEqual(shouldTryWebSocket({ hostname: "127.0.0.1" }), true);
assert.strictEqual(shouldTryWebSocket({ hostname: "bpforbes.github.io" }), true);
assert.strictEqual(shouldTryWebSocket({ hostname: "bailey-forbes.com" }), true);
assert.strictEqual(shouldTryWebSocket({ hostname: "bpforbes.github.io", skipPublicRelay: true }), false);
assert.strictEqual(shouldTryWebSocket({ forceBroadcast: true, hostname: "127.0.0.1" }), false);
assert.strictEqual(shouldTryWebSocket({ relayUrl: "ws://127.0.0.1:9/ws", hostname: "example.com" }), true);
assert.strictEqual(wsUrl({ hostname: "bpforbes.github.io" }), "wss://flintstone.bailey-forbes.com/ws?room=lab");
assert.strictEqual(wsUrl({ hostname: "flintstone.bailey-forbes.com", relayRoom: "lab" }), "wss://flintstone.bailey-forbes.com/ws?room=lab");
assert.strictEqual(healthUrl("wss://flintstone.bailey-forbes.com/ws?room=lab"), "https://flintstone.bailey-forbes.com/relay-health");

class MockBroadcastChannel {
  static rooms = new Map();
  constructor(name) {
    this.name = name;
    this.onmessage = null;
    this.closed = false;
    if (!MockBroadcastChannel.rooms.has(name)) MockBroadcastChannel.rooms.set(name, new Set());
    MockBroadcastChannel.rooms.get(name).add(this);
  }
  postMessage(data) {
    const copy = JSON.parse(JSON.stringify(data));
    for (const peer of MockBroadcastChannel.rooms.get(this.name) || []) {
      if (peer === this || peer.closed || typeof peer.onmessage !== "function") continue;
      queueMicrotask(() => peer.onmessage({ data: copy }));
    }
  }
  close() {
    this.closed = true;
    const room = MockBroadcastChannel.rooms.get(this.name);
    if (room) room.delete(this);
  }
}

globalThis.BroadcastChannel = MockBroadcastChannel;

function collect(client) {
  const events = [];
  client.on(event => events.push(event));
  return events;
}

function chatLines(events) {
  return events.filter(e => e.type === "message").map(e => e.text);
}

function hellos(events) {
  return events.filter(e => e.type === "hello");
}

(async () => {
  const config = { relayRoom: "dup-test", forceBroadcast: true };
  const a = createRelayClient(config);
  const b = createRelayClient(config);
  const ae = collect(a);
  const be = collect(b);
  await a.connect("flinstone");
  await b.connect("flinstone");
  assert.strictEqual(hellos(ae).length, 1);
  assert.strictEqual(hellos(be).length, 1);

  assert(a.sendMessage("Hi"));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.deepStrictEqual(chatLines(ae), ["flinstone: Hi"]);
  assert.deepStrictEqual(chatLines(be), ["flinstone: Hi"]);

  const before = hellos(ae).length;
  await Promise.all([a.connect("flinstone"), a.connect("flinstone")]);
  assert.strictEqual(hellos(ae).length, before, "same-principal reconnect must not emit another hello");

  assert(a.sendMessage("Hi"));
  await new Promise(resolve => setTimeout(resolve, 20));
  assert.deepStrictEqual(chatLines(ae), ["flinstone: Hi", "flinstone: Hi"]);
  assert.deepStrictEqual(chatLines(be), ["flinstone: Hi", "flinstone: Hi"]);

  a.leave();
  b.leave();
  console.log("test_server_relay_client: PASS");
})().catch(error => {
  console.error(error);
  process.exit(1);
});
