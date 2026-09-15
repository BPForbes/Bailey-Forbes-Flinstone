"use strict";
const assert = require("assert");
const net = require("net");
const path = require("path");
const { spawn } = require("child_process");
const { createRequire } = require("module");
const labRequire = createRequire(path.join(__dirname, "../tools/browser-lab/package.json"));
const WebSocket = labRequire("ws");

globalThis.FlintstoneSessionWire = require("../tools/browser-lab/session-wire.js");
globalThis.WebSocket = WebSocket;
const { createRelayClient } = require("../tools/browser-lab/server-relay-client.js");

const port = Number(process.env.FL_SERVER_RELAY_CLIENTS_WS_PORT || 8775);
const url = `ws://127.0.0.1:${port}/ws?room=lab`;

function waitForPort(openPort, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const tryOnce = () => {
      if (Date.now() >= deadline) {
        reject(new Error(`relay port ${openPort} did not open within ${timeoutMs}ms`));
        return;
      }
      const probe = net.connect(openPort, "127.0.0.1");
      probe.once("connect", () => { probe.end(); resolve(); });
      probe.once("error", () => setTimeout(tryOnce, 50));
    };
    tryOnce();
  });
}

function collect(client) {
  const events = [];
  client.on(event => events.push(event));
  return events;
}

function chatLines(events) {
  return events.filter(e => e.type === "message").map(e => e.text);
}

(async () => {
  const hub = spawn("node", ["server-relay-hub.mjs", "--port", String(port), "--room", "lab"], {
    cwd: path.join(__dirname, "../tools/browser-lab"),
    stdio: ["ignore", "pipe", "pipe"],
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("relay hub startup timed out")), 15000);
    const ready = chunk => {
      if (String(chunk).includes("Session relay:")) {
        clearTimeout(timer);
        resolve();
      }
    };
    hub.stdout.on("data", ready);
    hub.stderr.on("data", ready);
    hub.once("error", reject);
  });
  await waitForPort(port);

  try {
    const health = await fetch(`http://127.0.0.1:${port}/relay-health`);
    assert.strictEqual(health.status, 200);
    assert.strictEqual(await health.text(), "ok");

    const alice = createRelayClient({ relayUrl: url, skipRelayHealth: true });
    const bob = createRelayClient({ relayUrl: url, skipRelayHealth: true });
    const aliceEvents = collect(alice);
    const bobEvents = collect(bob);
    await alice.connect("alice");
    await bob.connect("bob");
    assert(alice.sendMessage("hello from alice"));
    await new Promise(resolve => setTimeout(resolve, 80));
    assert.deepStrictEqual(chatLines(aliceEvents).filter(line => line.includes("hello from alice")), ["alice: hello from alice"]);
    assert.deepStrictEqual(chatLines(bobEvents).filter(line => line.includes("hello from alice")), ["alice: hello from alice"]);
    alice.leave();
    bob.leave();
    console.log("test_server_relay_clients_ws: PASS");
  } finally {
    hub.kill();
  }
})().catch(error => {
  console.error(error);
  process.exit(1);
});
