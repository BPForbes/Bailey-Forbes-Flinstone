/*! coi-serviceworker v0.1.6 - Guido Zuidhof, licensed under MIT */
let coepCredentialless = false;
if (typeof window === "undefined") {
  async function labDnsLookup(name) {
    const headers = new Headers({
      "Content-Type": "application/json",
      "Cross-Origin-Resource-Policy": "same-origin",
      "Cross-Origin-Embedder-Policy": "require-corp",
      "Cache-Control": "no-store",
    });
    const json = (status, body) => new Response(JSON.stringify(body), { status, headers });
    if (!name || name.length > 253 || /[^A-Za-z0-9.-]/.test(name))
      return json(400, { ok: false, error: "bad name" });
    const query = async type => {
      const response = await fetch("https://cloudflare-dns.com/dns-query?name=" + encodeURIComponent(name) + "&type=" + type, {
        headers: { Accept: "application/dns-json" },
      });
      if (!response.ok) return "";
      const body = await response.json();
      const rec = (body.Answer || []).find(row => row.type === (type === "A" ? 1 : 28));
      return rec && rec.data ? rec.data : "";
    };
    try {
      const [ip, ipv6] = await Promise.all([query("A"), query("AAAA")]);
      const ok = Boolean(ip || ipv6);
      return json(ok ? 200 : 404, { ok, name, ip: ip || "", ipv6: ipv6 || "" });
    } catch (_) {
      return json(502, { ok: false, error: "lookup failed" });
    }
  }
  self.addEventListener("install", () => self.skipWaiting());
  self.addEventListener("activate", event => event.waitUntil(self.clients.claim()));
  self.addEventListener("message", event => {
    if (event.data?.type === "coepCredentialless") coepCredentialless = event.data.value;
  });
  self.addEventListener("fetch", event => {
    const request = event.request;
    const reqUrl = new URL(request.url);
    if (reqUrl.origin === self.location.origin && reqUrl.pathname.endsWith("/lab-dns")) {
      event.respondWith(labDnsLookup(reqUrl.searchParams.get("name") || ""));
      return;
    }
    if (request.cache === "only-if-cached" && request.mode !== "same-origin") return;
    event.respondWith(fetch(coepCredentialless && request.mode === "no-cors"
      ? new Request(request, { credentials: "omit" }) : request).then(response => {
      if (response.status === 0) return response;
      const headers = new Headers(response.headers);
      headers.set("Cross-Origin-Embedder-Policy", coepCredentialless ? "credentialless" : "require-corp");
      // Top-level documents need COOP to become isolated. Framed documents must
      // not receive it: a first-visit reload that adds same-origin COOP moves the
      // iframe into a new browsing context group and Chromium replaces it with
      // chrome-error://chromewebdata/. Nested isolation comes from COEP plus the
      // parent's COOP/COEP and Permissions-Policy delegation. Sec-Fetch-Dest is
      // a forbidden header inside service workers; request.destination is
      // "document" for top-level navigations and "iframe" for framed ones.
      if (request.destination === "document") {
        headers.set("Cross-Origin-Opener-Policy", "same-origin");
      }
      return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
    }));
  });
} else if (window.crossOriginIsolated === false && window.isSecureContext && navigator.serviceWorker) {
  let reloading = false;
  const reloadOnce = () => {
    if (reloading) return;
    reloading = true;
    window.location.reload();
  };
  navigator.serviceWorker.addEventListener("controllerchange", reloadOnce, { once: true });
  navigator.serviceWorker.register(document.currentScript.src).then(registration => {
    if (registration.active && !navigator.serviceWorker.controller) reloadOnce();
  }).catch(error => console.error("COOP/COEP service worker failed", error));
}
