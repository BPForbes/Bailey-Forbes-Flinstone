import assert from "node:assert/strict";
import {
  applyChildHeaders, relayHealthResponse, handleLabFetch,
} from "../infra/cloudflare/flintstone-lab-worker.js";

function headersFor(fetchDest) {
  return applyChildHeaders(new Response("ok", { headers: { "X-Frame-Options": "DENY" } }), {
    pathname: "/",
    fetchDest,
  }).headers;
}

const documentHeaders = headersFor("document");
assert.equal(documentHeaders.get("Cross-Origin-Opener-Policy"), "same-origin");
assert.equal(documentHeaders.get("Cross-Origin-Embedder-Policy"), "require-corp");
assert.equal(documentHeaders.get("X-Frame-Options"), null);

const iframeHeaders = headersFor("iframe");
assert.equal(iframeHeaders.get("Cross-Origin-Opener-Policy"), null);
assert.equal(iframeHeaders.get("Cross-Origin-Embedder-Policy"), "require-corp");

const otherHeaders = headersFor("empty");
assert.equal(otherHeaders.get("Cross-Origin-Opener-Policy"), null);

const health = relayHealthResponse();
assert.equal(health.status, 200);
assert.equal(health.headers.get("Access-Control-Allow-Origin"), "*");
assert.equal(await health.text(), "ok");

const healthGet = await handleLabFetch(new Request("https://flintstone.bailey-forbes.com/relay-health"));
assert.equal(healthGet.status, 200);

const wsNoUpgrade = await handleLabFetch(new Request("https://flintstone.bailey-forbes.com/ws?room=lab", {
  headers: { Origin: "https://bpforbes.github.io" },
}));
assert.equal(wsNoUpgrade.status, 426);

const wsBadOrigin = await handleLabFetch(new Request("https://flintstone.bailey-forbes.com/ws?room=lab", {
  headers: { Origin: "https://evil.example", Upgrade: "websocket" },
}));
assert.equal(wsBadOrigin.status, 403);

const wsNoBinding = await handleLabFetch(new Request("https://flintstone.bailey-forbes.com/ws?room=lab", {
  headers: { Origin: "https://bpforbes.github.io", Upgrade: "websocket" },
}), {});
assert.equal(wsNoBinding.status, 503);

console.log("test_flintstone_lab_worker: PASS (document COOP, iframe omits COOP, relay health/ws)");
