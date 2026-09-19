"use strict";
const assert = require("assert");
const net = require("net");
const path = require("path");
const { spawn } = require("child_process");
const { createRequire } = require("module");
const labRequire = createRequire(path.join(__dirname, "../tools/browser-lab/package.json"));
const WebSocket = labRequire("ws");
const { OP, encodeFrame, FrameParser, payloadText } = require("../tools/browser-lab/session-wire.js");

const port = Number(process.env.FL_SERVER_RELAY_TEST_PORT || 8772);
const url = `ws://127.0.0.1:${port}/ws?room=test`;

function waitForPort(openPort, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const tryOnce = () => {
      if (Date.now() >= deadline) {
        reject(new Error(`relay port ${openPort} did not open within ${timeoutMs}ms`));
        return;
      }
      const probe = net.connect(openPort, "127.0.0.1");
      probe.once("connect", () => {
        probe.end();
        resolve();
      });
      probe.once("error", () => {
        setTimeout(tryOnce, 50);
      });
    };
    tryOnce();
  });
}

function connect(principal, attempts = 20) {
  return new Promise((resolve, reject) => {
    const tryConnect = remaining => {
      const socket = new WebSocket(url);
      const parser = new FrameParser();
      let settled = false;
      const fail = error => {
        if (settled) return;
        settled = true;
        try { socket.close(); } catch (_) { /* ignore */ }
        if (remaining > 0) {
          setTimeout(() => tryConnect(remaining - 1), 100);
          return;
        }
        reject(error);
      };
      socket.on("open", () => {
        socket.send(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, principal));
      });
      socket.on("message", data => {
        for (const frame of parser.push(new Uint8Array(data))) {
          if (frame.opcode === OP.FL_NET_SESSION_OP_HELLO_ACK) {
            if (settled) return;
            settled = true;
            resolve({ socket, frame, parser });
          }
        }
      });
      socket.on("error", fail);
    };
    tryConnect(attempts);
  });
}

(async () => {
  const hub = spawn("node", ["server-relay-hub.mjs", "--port", String(port), "--room", "test"], {
    cwd: path.join(__dirname, "../tools/browser-lab"),
    stdio: ["ignore", "pipe", "pipe"],
  });
  let hubFailed = null;
  hub.once("exit", code => {
    if (code !== 0 && code !== null) hubFailed = new Error(`relay hub exited: ${code}`);
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
  if (hubFailed) throw hubFailed;
  await waitForPort(port);

  try {
    const host = await connect("flinstone");
    const join = await connect("root");
    assert(payloadText(host.frame.payload.slice(2)).includes("flinstone"));
    join.socket.send(encodeFrame(OP.FL_NET_SESSION_OP_MSG, "relay ping"));
    const broadcast = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("missing MSG_BROADCAST")), 5000);
      host.socket.on("message", data => {
        for (const frame of host.parser.push(new Uint8Array(data))) {
          if (frame.opcode === OP.FL_NET_SESSION_OP_MSG_BROADCAST) {
            clearTimeout(timer);
            resolve(payloadText(frame.payload));
          }
        }
      });
    });
    assert(broadcast.includes("root") && broadcast.includes("relay ping"));
    console.log("test_server_relay: PASS (HELLO, relay MSG_BROADCAST matches net_server.c wire)");
  } finally {
    hub.kill();
  }
})().catch(error => {
  console.error(error);
  process.exit(1);
});
