import assert from "node:assert/strict";
import { applyChildHeaders } from "../infra/cloudflare/flintstone-lab-worker.js";

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

console.log("test_flintstone_lab_worker: PASS (document COOP, iframe omits COOP)");
