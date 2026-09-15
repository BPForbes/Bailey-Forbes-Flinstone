"use strict";
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const { STATES, validateManifest, isTrustedReadyEvent, createController, validDiagnosticVga, parseGuestLine, labDnsNameOk, labDnsRequestUrl, isFormTypingTarget, guestUserNameOk, guestRegisterLines, guestSwitchLines, guestNewSessionLines } = require("../tools/browser-lab/lab-core.js");
const { createQmpClient } = require("../tools/browser-lab/qmp-client.js");
const commit = "1".repeat(40);
const evidence = {
  commit, artifactSha256: "a".repeat(64), runtime: "ktock/qemu-wasm",
  runtimeCommit: "2".repeat(40), runtimeFiles: { "qemu.wasm": "b".repeat(64) },
  testedAt: "2026-09-14T00:00:00.000Z", browser: "Chromium 140",
  serial: "booting\r\nFLINTSTONE_KERNEL_BOOT_OK\r\n", checks: ["exact-marker", "vga-text-memory"],
};
const manifest = { schemaVersion: 2, commit, shortCommit: "1234567", architecture: "x86_64", browserEmulator: "test", artifact: "flintstone.img", sha256: "a".repeat(64), bootSuccessMarker: "FLINTSTONE_KERNEL_BOOT_OK", validationOutcome: "browser-bootable", bootableCandidate: true, bootable: true, v86Compatible: false, browserCompatible: true, bootSuccessMarkerImplemented: true, recommendedRamBytes: 64, blockers: [], capabilities: {}, browserValidation: evidence };
assert.strictEqual(validateManifest(manifest), manifest);
for (const bad of [
  { ...manifest, schemaVersion: 3 }, { ...manifest, artifact: "../bad" },
  { ...manifest, sha256: "bad" }, { ...manifest, browserCompatible: "true" },
  { ...manifest, bootableCandidate: "false" }, { ...manifest, browserValidation: {} },
  ...Object.keys(evidence).map(field => {
    const browserValidation = { ...evidence }; delete browserValidation[field];
    return { ...manifest, browserValidation };
  }),
  { ...manifest, browserValidation: { ...evidence, artifactSha256: "c".repeat(64) } },
  { ...manifest, browserValidation: { ...evidence, runtimeFiles: { "../qemu.wasm": "b".repeat(64) } } },
  { ...manifest, browserValidation: { ...evidence, checks: ["exact-marker"] } },
  { ...manifest, browserValidation: { ...evidence, serial: "FLINTSTONE_KERNEL_BOOT_OK suffix\n" } },
]) assert.throws(() => validateManifest(bad));
assert.strictEqual(validateManifest({ ...manifest, browserCompatible: false, browserValidation: undefined }).browserCompatible, false);

const vga = new Uint8Array(4000);
vga[0] = 0x46; vga[1] = 0x07;
assert(validDiagnosticVga(vga));
const statusBar = new Uint8Array(4000);
statusBar[0] = 0x46; statusBar[1] = 0x07;
statusBar[2] = 0x20; statusBar[3] = 0x0e;
assert(validDiagnosticVga(statusBar));
assert(!validDiagnosticVga(new Uint8Array(0)));
assert(!validDiagnosticVga(new Uint8Array(4000)));
const wrongGlyph = new Uint8Array(4000); wrongGlyph[0] = 0x58; wrongGlyph[1] = 0x07;
assert(!validDiagnosticVga(wrongGlyph));
const wrongAttr = new Uint8Array(4000); wrongAttr[0] = 0x46; wrongAttr[1] = 0x1f;
assert(!validDiagnosticVga(wrongAttr));
assert.deepStrictEqual(parseGuestLine("SESSION 2 user=root\r"), { type: "session", session: 2, user: "root" });
assert.deepStrictEqual(parseGuestLine("SWITCHUSER user=flinstone"), { type: "switchuser", user: "flinstone" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY host"), { type: "server", op: "host" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY msg hello room"), { type: "server", op: "msg", text: "hello room" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY announce hi"), { type: "server", op: "announce", text: "hi" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY kill"), { type: "server", op: "kill" });
assert.deepStrictEqual(parseGuestLine("SERVER_RELAY dns example.com"), { type: "dns", host: "example.com" });
assert.strictEqual(parseGuestLine("WHOAMI flinstone"), null);
assert(labDnsNameOk("example.com"));
assert(labDnsNameOk("bailey-forbes.com"));
assert(!labDnsNameOk("bad host"));
assert.strictEqual(guestRegisterLines("alice", "secret"), "switchuser root\nuseradd alice\nsecret\n");
assert.strictEqual(guestSwitchLines("alice"), "switchuser alice\n");
assert.strictEqual(guestNewSessionLines("alice"), "session new\nswitchuser alice\n");
assert.strictEqual(guestNewSessionLines(""), "session new\nswitchuser flinstone\n");
assert(!guestUserNameOk("bad user"));
assert(isFormTypingTarget({ nodeType: 1, tagName: "INPUT", isContentEditable: false }));
assert(!isFormTypingTarget({ nodeType: 1, tagName: "DIV", isContentEditable: false }));
const labJsSrc = fs.readFileSync("tools/browser-lab/lab.js", "utf8");
assert(!labJsSrc.includes("appendChat(`${relay.display}: ${text}`)"), "chat form must not locally echo; sendMessage emits once");
assert(!labJsSrc.includes("appendChat(`${relay.display || principal()}: ${event.text || \"\"}`)"), "guest SERVER_RELAY msg must not double-append");
assert(labJsSrc.includes("sendGuestLines"), "identity and commands must send guest lines one at a time");
assert(labJsSrc.includes("syncRelayPrincipal"), "switchuser must reconnect the relay seat");
assert(labJsSrc.includes("sendingMsg"), "chat submit must ignore a second submit while sending");
assert.strictEqual(
  labDnsRequestUrl("http://127.0.0.1:8766/tools/browser-lab/?validate=1", "example.com").href,
  "http://127.0.0.1:8766/tools/browser-lab/lab-dns?name=example.com"
);

const labHtml = fs.readFileSync("tools/browser-lab/index.html", "utf8");
assert(labHtml.includes('href="lab.css"'), "lab chrome stylesheet must be linked");
assert(labHtml.includes('id="powerline"'), "Liquid Glass chrome must include a Powerline status line");
assert(labHtml.includes('id="guest-cmd"'), "guest command box must exist");
assert(labHtml.includes('id="account-register"'), "user registrar must exist");
const labCss = fs.readFileSync("tools/browser-lab/lab.css", "utf8");
assert(labCss.includes("backdrop-filter"), "panels must use a glass blur");
assert(labCss.includes("JetBrains Mono"), "terminal chrome must request a Powerline-capable mono");
assert(labCss.includes("display-p3"), "24-bit / Display P3 accents must be declared");
assert(labCss.includes("clip-path"), "Powerline separators must be geometric, not overlapping glyphs");
assert(fs.existsSync("tools/browser-lab/fonts/nerd-symbols-powerline.woff2"));
assert(fs.existsSync("tools/browser-lab/fonts/jetbrains-mono-latin-wght-normal.woff2"));
const labJs = fs.readFileSync("tools/browser-lab/lab.js", "utf8");
assert(labJs.includes("VGA_TRUECOLOR"), "VGA renderer must use a 24-bit palette");
assert((labJs.match(/#[0-9a-fA-F]{6}/g) || []).length >= 16, "truecolor palette needs 16 hex slots");

let controllerChange;
let controllerOptions;
let reloads = 0;
vm.runInNewContext(fs.readFileSync("tools/browser-lab/coi-serviceworker.js", "utf8"), {
  window: { crossOriginIsolated: false, isSecureContext: true, location: { reload: () => reloads++ } },
  document: { currentScript: { src: "https://lab.example/coi-serviceworker.js" } },
  navigator: { serviceWorker: {
    controller: null,
    addEventListener: (name, listener, options) => {
      assert.strictEqual(name, "controllerchange");
      controllerChange = listener;
      controllerOptions = options;
    },
    register: async () => ({ active: null }),
  } },
  console,
});
assert.strictEqual(controllerOptions.once, true);
controllerChange();
controllerChange();
assert.strictEqual(reloads, 1);
const coiSrc = fs.readFileSync("tools/browser-lab/coi-serviceworker.js", "utf8");
assert(coiSrc.includes('request.destination === "document"'), "SW must set COOP only on top-level documents");
assert(coiSrc.includes('pathname.endsWith("/lab-dns")'), "SW must answer same-origin /lab-dns");
assert(!/headers\.set\("Cross-Origin-Opener-Policy", "same-origin"\);\s*return new Response/.test(coiSrc),
  "SW must not set COOP on every fetch, including iframe navigations");

const guestWindow = {};
const ready = { origin: "https://lab.example", source: guestWindow, data: { source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: "1234567" } };
const trust = { allowedOrigin: "https://lab.example", guestWindow, commit: "1234567" };
assert(isTrustedReadyEvent(ready, trust));
assert(!isTrustedReadyEvent({ ...ready, origin: "https://evil.example" }, trust));
assert(!isTrustedReadyEvent({ ...ready, source: {} }, trust));
assert(!isTrustedReadyEvent({ ...ready, data: { ...ready.data, schemaVersion: 2 } }, trust));
assert(!isTrustedReadyEvent({ ...ready, data: { ...ready.data, type: "loading" } }, trust));

(async () => {
  const fetchCalls = [];
  let fetchHandler;
  const swSelf = {
    location: { origin: "https://lab.example" },
    addEventListener: (name, listener) => {
      if (name === "fetch") fetchHandler = listener;
    },
    skipWaiting: () => {},
    clients: { claim: () => {} },
  };
  const hrefOf = value => typeof value === "string" ? value : (value && value.url) || String(value);
  vm.runInNewContext(coiSrc, {
    window: undefined,
    self: swSelf,
    fetch: async (url) => {
      const href = hrefOf(url);
      fetchCalls.push(href);
      if (href.includes("/lab-dns"))
        return { ok: false, status: 404, headers: new Headers(), json: async () => ({ ok: false }) };
      const type = href.includes("type=AAAA") ? 28 : 1;
      return {
        ok: true,
        json: async () => ({ Answer: type === 1 ? [{ type: 1, data: "93.184.216.34" }] : [] }),
      };
    },
    Headers, Request, Response, URL, URLSearchParams, console, Promise,
  });
  let answered;
  fetchHandler({
    request: new Request("https://lab.example/tools/browser-lab/lab-dns?name=example.com"),
    respondWith: (p) => { answered = p; },
  });
  const dnsResp = await answered;
  assert.strictEqual(dnsResp.status, 200);
  const dnsBody = await dnsResp.json();
  assert.deepStrictEqual(dnsBody, { ok: true, name: "example.com", ip: "93.184.216.34", ipv6: "" });
  assert(fetchCalls.some(u => u.includes("/lab-dns")));
  assert(fetchCalls.some(u => u.includes("cloudflare-dns.com") && u.includes("type=A")));

  const directCalls = [];
  let directHandler;
  vm.runInNewContext(coiSrc, {
    window: undefined,
    self: {
      location: { origin: "https://lab.example" },
      addEventListener: (name, listener) => { if (name === "fetch") directHandler = listener; },
      skipWaiting: () => {},
      clients: { claim: () => {} },
    },
    fetch: async (url) => {
      const href = hrefOf(url);
      directCalls.push(href);
      if (href.includes("/lab-dns"))
        return new Response(JSON.stringify({ ok: true, name: "example.com", ip: "93.184.216.34", ipv6: "" }), {
          status: 200, headers: { "Content-Type": "application/json" },
        });
      throw new Error("DoH should not run when same-origin /lab-dns succeeds: " + href);
    },
    Headers, Request, Response, URL, URLSearchParams, console, Promise,
  });
  let directAnswered;
  directHandler({
    request: new Request("https://lab.example/tools/browser-lab/lab-dns?name=example.com"),
    respondWith: (p) => { directAnswered = p; },
  });
  const directResp = await directAnswered;
  assert.strictEqual(directResp.status, 200);
  assert.deepStrictEqual(await directResp.json(), { ok: true, name: "example.com", ip: "93.184.216.34", ipv6: "" });
  assert(directCalls.every(u => !u.includes("cloudflare-dns.com")));

  const sent = [];
  const qmp = createQmpClient({ send: cmd => sent.push(cmd), timeoutMs: 30 });
  const earlyCont = qmp.command("cont");
  const capabilities = qmp.command("qmp_capabilities");
  const dump = qmp.command("pmemsave", { val: 1, size: 2, filename: "/screen.bin" });
  const resume = qmp.command("cont");
  assert.deepStrictEqual(sent, []);
  assert.strictEqual(qmp.accept({ QMP: { version: {} } }), "greeting");
  assert.strictEqual(sent.length, 1);
  assert.strictEqual(sent[0].execute, "qmp_capabilities");
  qmp.accept({ return: {}, id: sent[0].id });
  await capabilities;
  qmp.allowWork();
  assert.strictEqual(sent[1].execute, "cont");
  assert.strictEqual(sent.length, 2);
  qmp.accept({ return: {}, id: sent[1].id });
  await earlyCont;
  assert.strictEqual(sent[2].execute, "pmemsave");
  qmp.accept({ return: {}, id: sent[2].id });
  await dump;
  assert.strictEqual(sent[3].execute, "cont");
  qmp.accept({ return: {}, id: sent[3].id });
  await resume;
  const timed = createQmpClient({ send() {}, timeoutMs: 20 });
  timed.accept({ QMP: { version: {} } });
  timed.allowWork();
  await assert.rejects(() => timed.command("cont"), /QEMU command timed out: cont/);
  const afterTimeout = [];
  const recovered = createQmpClient({ send: cmd => afterTimeout.push(cmd), timeoutMs: 5000 });
  recovered.accept({ QMP: { version: {} } });
  recovered.allowWork();
  await assert.rejects(() => recovered.command("pmemsave", { filename: "/screen.bin" }, 20), /QEMU command timed out: pmemsave/);
  const sendKey = recovered.command("send-key", { keys: [{ type: "qcode", data: "ret" }] });
  assert.strictEqual(afterTimeout.at(-1).execute, "send-key");
  recovered.accept({ return: {}, id: afterTimeout.at(-1).id });
  await sendKey;
  const adapterSrc = fs.readFileSync("tools/browser-lab/qemu-adapter.js", "utf8");
  assert(adapterSrc.includes("withScreenHeld(() => sendKey(qcodes))"), "send-key must hold VGA pmemsave");
  assert(adapterSrc.includes("pulseInputHold"), "key bursts must debounce VGA pmemsave");
  assert(adapterSrc.includes("setTimeout(resolve, ch === \"\\n\" || ch === \"\\r\" ? 80 : 20)"), "sendText must pace keys so the 8042 can drain");
  assert(adapterSrc.includes('filename: "/screen.bin" }, 3000)'), "pmemsave must use a short QMP timeout");
  const labSrc = fs.readFileSync("tools/browser-lab/lab.js", "utf8");
  assert(!/sendKey\(codes\)\)\.catch\(error => controller\.fail/.test(labSrc), "a send-key timeout must not fail Guest State");
  const cancelled = createQmpClient({ send() {}, timeoutMs: 5000 });
  const pending = cancelled.command("qmp_capabilities");
  cancelled.failAll(new Error("QEMU powered off"));
  await assert.rejects(pending, /QEMU powered off/);

  const states = []; let ready = 0; let created = 0; let destroyed = 0;
  let stopCalls = 0; let runCalls = 0;
  const emulator = { stop() { stopCalls++; }, run() { runCalls++; }, async destroy() { destroyed++; } };
  const controller = createController({ marker: manifest.bootSuccessMarker, setState: (s) => states.push(s), postReady: () => ready++, createEmulator: async ({ serialByte }) => { created++; emulator.serialByte = serialByte; return emulator; } });
  await controller.boot({});
  assert.strictEqual(await controller.resume(), false);
  assert.strictEqual(runCalls, 0);
  assert.strictEqual(await controller.pause(), false);
  assert.strictEqual(stopCalls, 0);
  "premature FLINTSTONE_KERNEL_BOOT_OK suffix\n".split("").forEach(emulator.serialByte);
  assert.strictEqual(ready, 0);
  "FLINTSTONE_KERNEL_BOOT_OK\r\n".split("").forEach(emulator.serialByte);
  assert.strictEqual(ready, 1); assert.strictEqual(states.at(-1), STATES.READY);
  assert(await controller.pause()); assert.strictEqual(states.at(-1), STATES.PAUSED);
  assert.strictEqual(stopCalls, 1);
  assert.strictEqual(await controller.pause(), false);
  assert.strictEqual(stopCalls, 1);
  assert(await controller.resume()); assert.strictEqual(states.at(-1), STATES.READY);
  assert.strictEqual(runCalls, 1);
  assert.strictEqual(await controller.resume(), false);
  assert.strictEqual(runCalls, 1);
  await controller.reset({}); assert.strictEqual(created, 2); assert.strictEqual(destroyed, 1); assert.strictEqual(states.at(-1), STATES.BOOTING);
  await controller.powerOff(); assert.strictEqual(states.at(-1), STATES.OFF);
  const failing = createController({ marker: "x", setState: (s) => states.push(s), postReady() {}, createEmulator: async () => { throw new Error("corrupt image"); } });
  await assert.rejects(() => failing.boot({}), /corrupt image/); assert.strictEqual(states.at(-1), STATES.FAILED);
  console.log("test_browser_lab: PASS");
})().catch((error) => { console.error(error); process.exitCode = 1; });
