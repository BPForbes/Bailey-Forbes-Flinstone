/**
 * Proxy GitHub Pages browser lab with cross-origin isolation headers for
 * iframe embed on https://bailey-forbes.com (see docs/cloudflare-flintstone-lab-deploy.md).
 *
 * Child COOP is omitted on iframe navigations — COOP: same-origin on framed
 * documents triggers chrome-error://chromewebdata/ in Chromium.
 *
 * `/ws?room=` is a Durable Object WebSocket hub so visitors on Pages and the
 * Worker origin share one server-chat room. `/relay-health` is a CORS probe
 * the lab client uses before opening WebSocket.
 */
import { Room, attachSocket } from "../../tools/browser-lab/session-relay-room.js";

const ORIGIN = "https://bpforbes.github.io";
const BASE_PATH = "/Bailey-Forbes-Flinstone";
const FRAME_ANCESTOR = "https://bailey-forbes.com";

const CHILD_HEADERS = {
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Resource-Policy": "cross-origin",
  "Content-Security-Policy": `frame-ancestors ${FRAME_ANCESTOR}`,
};

const STRIP_RESPONSE_HEADERS = [
  "content-security-policy",
  "cross-origin-embedder-policy",
  "cross-origin-opener-policy",
  "cross-origin-resource-policy",
  "x-frame-options",
];

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, HEAD, OPTIONS",
  "Access-Control-Allow-Headers": "Upgrade, Connection, Sec-WebSocket-Key, Sec-WebSocket-Version, Sec-WebSocket-Protocol",
  "Cache-Control": "no-store",
};

function originAllowed(origin) {
  if (!origin) return true;
  try {
    const url = new URL(origin);
    if (url.hostname === "flintstone.bailey-forbes.com") return true;
    if (url.hostname === "bpforbes.github.io") return true;
    if (url.hostname.endsWith(".workers.dev") && url.hostname.includes("flintstone")) return true;
    if (url.hostname === "127.0.0.1" || url.hostname === "localhost" || url.hostname === "::1") return true;
    return false;
  } catch (_) {
    return false;
  }
}

function upstreamUrl(requestUrl) {
  const incoming = new URL(requestUrl);
  let path = incoming.pathname;
  if (path === "/" || path === "") {
    path = `${BASE_PATH}/`;
  } else if (!path.startsWith(BASE_PATH)) {
    path = `${BASE_PATH}${path}`;
  }
  const target = new URL(path, ORIGIN);
  target.search = incoming.search;
  return target.toString();
}

function isNoStorePath(pathname) {
  return (
    pathname.endsWith("/build-info.json") ||
    pathname.endsWith("/browser-validation.json") ||
    pathname.endsWith("/project-metadata.json")
  );
}

export function applyChildHeaders(response, { pathname, fetchDest }) {
  const headers = new Headers(response.headers);
  for (const name of STRIP_RESPONSE_HEADERS) {
    headers.delete(name);
  }
  for (const [name, value] of Object.entries(CHILD_HEADERS)) {
    headers.set(name, value);
  }
  if (fetchDest === "iframe") {
    headers.delete("Cross-Origin-Opener-Policy");
  } else if (fetchDest === "document") {
    headers.set("Cross-Origin-Opener-Policy", "same-origin");
  } else {
    headers.delete("Cross-Origin-Opener-Policy");
  }
  if (isNoStorePath(pathname)) {
    headers.set("Cache-Control", "no-store");
  }
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
}

export function relayHealthResponse() {
  return new Response("ok", {
    status: 200,
    headers: { ...CORS_HEADERS, "content-type": "text/plain; charset=utf-8" },
  });
}

export class LabRelayRoom {
  constructor(_ctx, _env) {
    this.room = new Room("lab");
  }
  async fetch(request) {
    if ((request.headers.get("Upgrade") || "").toLowerCase() !== "websocket") {
      return new Response("Expected WebSocket", { status: 426 });
    }
    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    server.accept();
    const handler = attachSocket(this.room, server);
    server.addEventListener("message", event => handler.onMessage(event.data));
    server.addEventListener("close", () => handler.onClose());
    server.addEventListener("error", () => handler.onClose());
    return new Response(null, { status: 101, webSocket: client });
  }
}

export async function handleLabFetch(request, env) {
  const incoming = new URL(request.url);
  const path = incoming.pathname.replace(/\/+$/, "") || "/";

  if (path === "/relay-health") {
    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: CORS_HEADERS });
    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("Method not allowed", { status: 405, headers: CORS_HEADERS });
    }
    return relayHealthResponse();
  }

  if (path === "/ws") {
    const origin = request.headers.get("Origin") || "";
    if (!originAllowed(origin)) return new Response("origin not allowed", { status: 403 });
    if ((request.headers.get("Upgrade") || "").toLowerCase() !== "websocket") {
      return new Response("Expected WebSocket", { status: 426 });
    }
    if (!env || !env.LAB_RELAY) return new Response("relay unavailable", { status: 503 });
    const room = incoming.searchParams.get("room") || "lab";
    const id = env.LAB_RELAY.idFromName(room);
    return env.LAB_RELAY.get(id).fetch(request);
  }

  if (request.method !== "GET" && request.method !== "HEAD") {
    return new Response("Method not allowed", { status: 405 });
  }

  const upstream = upstreamUrl(request.url);
  const upstreamRequest = new Request(upstream, {
    method: request.method,
    headers: request.headers,
    redirect: "follow",
  });

  let response;
  try {
    response = await fetch(upstreamRequest, {
      cf: { cacheEverything: !isNoStorePath(incoming.pathname) },
    });
  } catch (error) {
    return new Response(`Upstream fetch failed: ${error}`, { status: 502 });
  }

  const fetchDest = request.headers.get("Sec-Fetch-Dest") || "";
  return applyChildHeaders(response, {
    pathname: incoming.pathname,
    fetchDest,
  });
}

export default {
  async fetch(request, env, ctx) {
    return handleLabFetch(request, env, ctx);
  },
};
