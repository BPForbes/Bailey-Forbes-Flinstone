"use strict";
const assert = require("assert");
const path = require("path");
const { spawn } = require("child_process");
const { createRequire } = require("module");
const labRequire = createRequire(path.join(__dirname, "../tools/browser-lab/package.json"));
const WebSocket = labRequire("ws");
const { OP, encodeFrame, FrameParser, payloadText } = require("../tools/browser-lab/session-wire.js");

const port = Number(process.env.FL_SERVER_RELAY_TEST_PORT || 8772);
const url = `ws://127.0.0.1:${port}/ws?room=test`;

function connect(principal) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    const parser = new FrameParser();
    socket.on("open", () => {
      socket.send(encodeFrame(OP.FL_NET_SESSION_OP_HELLO, principal));
    });
    socket.on("message", data => {
      for (const frame of parser.push(new Uint8Array(data))) {
        if (frame.opcode === OP.FL_NET_SESSION_OP_HELLO_ACK) {
          resolve({ socket, frame, parser });
        }
      }
    });
    socket.on("error", reject);
  });
}

(async () => {
  const hub = spawn("node", ["server-relay-hub.mjs", "--port", String(port), "--room", "test"], {
    cwd: path.join(__dirname, "../tools/browser-lab"),
    stdio: ["ignore", "pipe", "pipe"],
  });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("relay hub startup timed out")), 5000);
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
