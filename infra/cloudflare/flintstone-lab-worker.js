/**
 * Proxy GitHub Pages browser lab with cross-origin isolation headers for
 * iframe embed on https://bailey-forbes.com (see docs/cloudflare-flintstone-lab-deploy.md).
 *
 * Child COOP is omitted on iframe navigations — COOP: same-origin on framed
 * documents triggers chrome-error://chromewebdata/ in Chromium.
 */
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
    pathname.endsWith("/browser-validation.json")
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

export default {
  async fetch(request, env, ctx) {
    const incoming = new URL(request.url);
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
  },
};
