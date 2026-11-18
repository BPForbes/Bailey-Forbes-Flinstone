#!/usr/bin/env node
/**
 * Browser-lab session hub — speaks the same P3 session wire as net_server.c.
 * Used only when runtimeMode=browser-hosted (online lab). Local VM/bare-metal
 * shells use kernel/core/net (C/ASM) and never connect here.
 */
import { createRequire } from "node:module";
import { createServer } from "node:http";

const require = createRequire(import.meta.url);
const { WebSocketServer } = require("ws");
const { Room, attachSocket } = require("./session-relay-room.js");

function parseArgs(argv) {
  const out = { bind: "127.0.0.1", port: 8767, room: "lab" };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === "--bind") out.bind = argv[++i];
    else if (argv[i] === "--port") out.port = Number(argv[++i]);
    else if (argv[i] === "--room") out.room = argv[++i];
  }
  return out;
}

const rooms = new Map();

function roomFor(id) {
  if (!rooms.has(id)) rooms.set(id, new Room(id));
  return rooms.get(id);
}

function main() {
  const args = parseArgs(process.argv);
  const server = createServer((req, res) => {
    const path = (req.url || "/").split("?")[0];
    if (path === "/relay-health" || path === "/relay-health/") {
      res.writeHead(200, {
        "content-type": "text/plain; charset=utf-8",
        "access-control-allow-origin": "*",
        "cache-control": "no-store",
      });
      res.end("ok");
      return;
    }
    res.writeHead(404);
    res.end();
  });
  const wss = new WebSocketServer({ server, path: "/ws" });
  wss.on("connection", (socket, req) => {
    const url = new URL(req.url || "/ws", "http://localhost");
    const roomId = url.searchParams.get("room") || args.room;
    const room = roomFor(roomId);
    const handler = attachSocket(room, socket);
    socket.on("message", data => handler.onMessage(data));
    socket.on("close", () => handler.onClose());
  });
  server.listen(args.port, args.bind, () => {
    console.log(`Session relay: ws://${args.bind}:${args.port}/ws (room=${args.room})`);
  });
}

main();
