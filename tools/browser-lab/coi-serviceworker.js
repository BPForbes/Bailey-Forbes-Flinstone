/*! coi-serviceworker v0.1.6 - Guido Zuidhof, licensed under MIT */
let coepCredentialless = false;
if (typeof window === "undefined") {
  self.addEventListener("install", () => self.skipWaiting());
  self.addEventListener("activate", event => event.waitUntil(self.clients.claim()));
  self.addEventListener("message", event => {
    if (event.data?.type === "coepCredentialless") coepCredentialless = event.data.value;
  });
  self.addEventListener("fetch", event => {
    const request = event.request;
    if (request.cache === "only-if-cached" && request.mode !== "same-origin") return;
    event.respondWith(fetch(coepCredentialless && request.mode === "no-cors"
      ? new Request(request, { credentials: "omit" }) : request).then(response => {
      if (response.status === 0) return response;
      const headers = new Headers(response.headers);
      headers.set("Cross-Origin-Embedder-Policy", coepCredentialless ? "credentialless" : "require-corp");
      headers.set("Cross-Origin-Opener-Policy", "same-origin");
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
