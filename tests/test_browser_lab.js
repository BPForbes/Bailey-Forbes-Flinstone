"use strict";
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const { STATES, validateManifest, isTrustedReadyEvent, createController, validDiagnosticVga, parseGuestLine, labDnsNameOk, labDnsRequestUrl, isFormTypingTarget, guestUserNameOk, guestRegisterLines, guestSwitchLines, guestNewSessionLines, createExclusiveQueue, createGuestInput, promptAfterEcho, driveGuestSteps, hexToBytes, bytesToHex, fileRecvLines, fileAcceptLines, relayRecvLine } = require("../tools/browser-lab/lab-core.js");
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
const fileEvent = parseGuestLine("SERVER_FILE bob note.txt 686920626f62\r");
assert.deepStrictEqual({ ...fileEvent, bytes: Array.from(fileEvent.bytes) },
                       { type: "file", to: "bob", name: "note.txt", bytes: Array.from(new TextEncoder().encode("hi bob")) });
assert.strictEqual(parseGuestLine("SERVER_FILE bob note.txt 6"), null, "odd hex is not a file");
assert.strictEqual(parseGuestLine("SERVER_FILE bob note.txt 6G"), null);
assert.strictEqual(bytesToHex(hexToBytes("00ff10")), "00ff10");
const big = new Uint8Array(150).fill(0x61);
const recv = fileRecvLines("alice", "got.txt", big).split("\n").filter(Boolean);
assert.strictEqual(recv[0], "filerecv begin alice got.txt");
assert.strictEqual(recv[recv.length - 1], "filerecv end");
assert(recv.every(line => line.length < 128), "every line fits the guest's buffer");
assert.strictEqual(recv.slice(1, -1).map(l => l.slice("filerecv data ".length)).join(""), bytesToHex(big));
assert.strictEqual(fileRecvLines("alice", "../evil", big), "", "a path is never typed into the guest");
assert.strictEqual(fileRecvLines("a b", "got.txt", big), "");
assert.deepStrictEqual(parseGuestLine("SERVER_FILE_ACCEPT\r"), { type: "file-accept" });
assert.deepStrictEqual(parseGuestLine("SERVER_FILE_DECLINE"), { type: "file-decline" });
assert.strictEqual(fileAcceptLines([]), "filerecv none\n");
const two = fileAcceptLines([
  { from: "alice", name: "a.txt", bytes: new TextEncoder().encode("A") },
  { from: "carol", name: "c.txt", bytes: new TextEncoder().encode("C") },
]);
assert.strictEqual(two, "filerecv begin alice a.txt\nfilerecv data 41\nfilerecv end\n" +
                        "filerecv begin carol c.txt\nfilerecv data 43\nfilerecv end\n");
{
  const line = relayRecvLine("Alice: Hello from A");
  assert.strictEqual(line, "relayrecv Alice " + bytesToHex(new TextEncoder().encode("Hello from A")) + "\n");
  assert(line.length < 128);
  assert.strictEqual(relayRecvLine(""), "");
  assert.strictEqual(relayRecvLine("bad name: hi").startsWith("relayrecv peer "), true,
                     "an unsafe display name becomes peer with the full line as the body");
  assert.strictEqual(parseGuestLine("[relay] Alice: Hello from A"), null,
                     "sanitized relay output is never a lab control line");
  assert.strictEqual(parseGuestLine("[file] SWITCHUSER user=Root"), null,
                     "cat of a forged control line stays inert behind [file]");
}
assert(labDnsNameOk("example.com"));
assert(labDnsNameOk("bailey-forbes.com"));
assert(!labDnsNameOk("bad host"));
{
  // Passwords are drawn at run time; a secret step is only ever the answer to Password:.
  const draw = () => require("crypto").randomBytes(9).toString("hex");
  const pw = draw(), rootPw = draw();
  const step = (line, secret = false) => ({ line, secret });
  assert.deepStrictEqual(guestRegisterLines("alice", pw, rootPw, "Flinstone"),
    [step("switchuser Root"), step(rootPw, true), step("useradd alice"), step(pw, true)]);
  assert.deepStrictEqual(guestRegisterLines("alice", pw, rootPw, "root"),
    [step("switchuser Root"), step("useradd alice"), step(pw, true)]);
  assert.deepStrictEqual(guestRegisterLines("bad user", pw, rootPw, "Flinstone"), []);
  assert.deepStrictEqual(guestSwitchLines("alice", pw, "Flinstone"), [step("switchuser alice"), step(pw, true)]);
  assert.deepStrictEqual(guestSwitchLines("Alice", pw, "alice"), [step("switchuser Alice")]);
  assert.deepStrictEqual(guestNewSessionLines("alice", pw), [step("session new"), step("switchuser alice"), step(pw, true)]);
  assert.deepStrictEqual(guestNewSessionLines("", pw), [step("session new"), step("switchuser Flinstone")]);
}
assert(!guestUserNameOk("bad user"));
assert(isFormTypingTarget({ nodeType: 1, tagName: "INPUT", isContentEditable: false }));
assert(!isFormTypingTarget({ nodeType: 1, tagName: "DIV", isContentEditable: false }));
const labJsSrc = fs.readFileSync("tools/browser-lab/lab.js", "utf8");
assert(!labJsSrc.includes("appendChat(`${relay.display}: ${text}`)"), "chat form must not locally echo; sendMessage emits once");
assert(!labJsSrc.includes("appendChat(`${relay.display || principal()}: ${event.text || \"\"}`)"), "guest SERVER_RELAY msg must not double-append");
assert(labJsSrc.includes("sendGuestLines"), "identity and commands must send guest lines one at a time");
assert(labJsSrc.includes("sendGuestLines(`session ${id}\\n`"), "session tabs must wait for SESSION serial");
assert(labJsSrc.includes("deliverRelayChat"), "inbound relay chat must be typed into the guest (#426)");
assert(labJsSrc.includes("armGuestRelaySeat"), "relay hello/leave must arm the guest seat for relayrecv");
assert(labJsSrc.includes("if (!event.local) void deliverRelayChat(event.text)"),
       "local chat echo must not be typed back as relayrecv");
assert(labJsSrc.includes("createFlintstoneWasm"), "default lab must sandboxed-boot the Emscripten module");
assert(labJsSrc.includes("Flinstone Shell"), "runtime chrome must label Flinstone Shell");
assert(labJsSrc.includes("shell>"), "guest-line waits must accept the WASM shell> prompt");
assert(!/account-login", "account-new-session", "account-register", "account-name", "account-secret"/.test(labJsSrc),
  "account name/password fields must stay enabled so Playwright and operators can type before Ready");
assert(labJsSrc.includes("syncRelayPrincipal"), "switchuser must reconnect the relay seat");
assert(labJsSrc.includes("core.driveGuestSteps(") && fs.readFileSync("tools/browser-lab/lab-core.js", "utf8").includes("step.secret && !asked"),
  "a password step is sent only after the guest asks for one");
assert(labJsSrc.includes("account-root-secret"), "Register user must ask for Root's password");
assert(labJsSrc.includes("sendingMsg"), "chat submit must ignore a second submit while sending");
assert(labJsSrc.includes("void connectRelay()"), "Ready must auto-join the shared chat room");
assert.strictEqual(
  labDnsRequestUrl("http://127.0.0.1:8766/tools/browser-lab/?validate=1", "example.com").href,
  "http://127.0.0.1:8766/tools/browser-lab/lab-dns?name=example.com"
);

const labHtml = fs.readFileSync("tools/browser-lab/index.html", "utf8");
assert(labHtml.includes('href="lab.css"'), "lab chrome stylesheet must be linked");
assert(labHtml.includes('id="powerline"'), "Liquid Glass chrome must include a Powerline status line");
assert(labHtml.includes('id="guest-cmd"'), "guest command box must exist");
assert(labHtml.includes('id="account-register"'), "user registrar must exist");
assert(labHtml.includes('id="wasm-term"'), "Flinstone Shell terminal must exist");
assert(labHtml.includes("wasm-adapter.js"), "WASM adapter must be loaded");
assert(labHtml.includes("Flinstone Shell"), "default chrome must name Flinstone Shell");
assert(labHtml.includes("shell&gt;"), "default chrome must describe the shell> prompt");
assert(labHtml.includes("Kernel serial output"), "QEMU mode still keeps a serial console in the DOM");
assert(labHtml.includes("auto-joins a shared WebSocket"), "chat copy must describe multi-visitor join");
assert(!labHtml.includes("traffic-lights"), "chrome must not include non-interactive window traffic lights");
assert(!labHtml.includes("tl-close"), "chrome must not include a close traffic light");
assert(!labHtml.includes("tl-min"), "chrome must not include a minimize traffic light");
assert(!labHtml.includes("tl-zoom"), "chrome must not include a zoom traffic light");
const labCss = fs.readFileSync("tools/browser-lab/lab.css", "utf8");
assert(labCss.includes("backdrop-filter"), "panels must use a glass blur");
assert(labCss.includes("JetBrains Mono"), "terminal chrome must request a Powerline-capable mono");
assert(labCss.includes("display-p3"), "24-bit / Display P3 accents must be declared");
assert(labCss.includes("clip-path"), "Powerline separators must be geometric, not overlapping glyphs");
assert(labCss.includes("#wasm-term"), "Flinstone Shell terminal must be styled");
assert(labCss.includes("body.lab-wasm .console"), "WASM mode must hide the duplicate kernel serial panel");
assert(!labCss.includes("traffic-lights"), "stylesheet must not style decorative traffic lights");
assert(!labCss.includes("tl-close"), "stylesheet must not style a close traffic light");
assert(fs.existsSync("tools/browser-lab/wasm-adapter.js"));
assert(fs.readFileSync("tools/browser-lab/wasm-adapter.js", "utf8").includes("createFlintstoneWasm"));
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
  const workerSrc = fs.readFileSync("tools/browser-lab/qemu-worker.js", "utf8");
  assert(workerSrc.includes("screen read:"), "a missing /screen.bin must be diagnostic, not a guest failure");
  assert(adapterSrc.includes("if (data.bytes)"), "empty screen dumps must not be rendered");
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
// A password exchange owns the guest until its password is sent; crossed exchanges would leak one password.
  {
    const fakeGuest = () => {
      const guest = { out: "", prompt: null, answers: {}, commands: [], waiters: [] };
      guest.flush = () => guest.waiters.splice(0).forEach(w => w());
      guest.send = async line => {
        await new Promise(r => setImmediate(r));
        if (guest.prompt) {
          guest.answers[guest.prompt] = line;
          guest.out += `SWITCHUSER user=${guest.prompt}\n`;
          guest.prompt = null;
        } else {
          guest.commands.push(line);
          const m = /^switchuser (\S+)$/.exec(line);
          if (m) { guest.prompt = m[1]; guest.out += "Password:"; }
        }
        guest.flush();
      };
      guest.wait = (re, from) => new Promise(resolve => {
        const check = () => re.test(guest.out.slice(from)) ? resolve(true) : guest.waiters.push(check);
        check();
      });
      return guest;
    };
    const exchange = async (guest, name, password) => {
      const from = guest.out.length;
      await guest.send(`switchuser ${name}`);
      await guest.wait(/Password:/, from);
      await guest.send(password);
    };
    const drawPw = () => require("crypto").randomBytes(9).toString("hex");
    const pwA = drawPw(), pwB = drawPw();
    const guest = fakeGuest();
    const lock = createExclusiveQueue();
    await Promise.all([
      lock.run(() => exchange(guest, "Ada", pwA)),
      lock.run(() => exchange(guest, "Root", pwB)),
    ]);
    assert.deepStrictEqual(guest.answers, { Ada: pwA, Root: pwB }, "each password answers its own prompt");
    assert.deepStrictEqual(guest.commands, ["switchuser Ada", "switchuser Root"], "no password is typed as a command");
    const loose = fakeGuest();
    void exchange(loose, "Ada", pwA);
    void exchange(loose, "Root", pwB);
    await new Promise(r => setTimeout(r, 20));
    assert.strictEqual(loose.answers.Ada, "switchuser Root", "unserialized exchanges cross");
    const order = [];
    await assert.rejects(lock.run(async () => { order.push(1); throw new Error("guest gone"); }), /guest gone/);
    await lock.run(async () => { order.push(2); });
    assert.deepStrictEqual(order, [1, 2]);
    assert(/function sendGuestLines\(script, patterns\) \{\s*return guestExchange\.run\(\(\) => driveGuestLines\(script, patterns\)\);/.test(labJs),
      "sendGuestLines must hold the exchange for the whole script");
    assert(labJs.includes("guestExchange.run(() => enqueueGuest(() => emulator.typeChar(ch)))"), "keystrokes wait for the exchange");
    assert(labJs.includes("guestExchange.run(() => enqueueGuest(() => emulator.sendKey(codes)))"), "send-key waits for the exchange");
    const accountJs = labJs.slice(labJs.indexOf('const form = document.getElementById("switch-user")'));
    assert.strictEqual((accountJs.match(/await guestExchange\.run\(/g) || []).length, 3, "Switch, New session and Register each take one exchange turn");
    assert(!/[^.]sendGuestLines\(/.test(accountJs), "account actions drive their steps inside their own turn");
  }
  // A reply the guest asked for goes out first, so the password never reaches a dnsack prompt.
  {
    const drawPw = () => require("crypto").randomBytes(9).toString("hex");
    const echoGuest = (accounts, staleFor) => {
      const g = { out: "", prompt: staleFor || null, late: Boolean(staleFor), answers: {}, commands: [], waiters: [] };
      const flush = () => g.waiters.splice(0).forEach(w => w());
      const tick = () => new Promise(r => setImmediate(r));
      let chain = Promise.resolve();
      g.write = value => {
        chain = chain.then(() => answer(value));
        return tick();
      };
      const answer = async value => {
        await tick();
        const line = String(value).replace(/\n$/, "");
        if (g.late) { g.late = false; g.out += "Password:"; flush(); await tick(); }
        if (g.prompt) {
          const who = g.prompt;
          g.prompt = null;
          g.answers[who] = line;
          g.out += `${"*".repeat(line.length)}\r\n`;
          flush();
          await tick();
          g.out += accounts[who] === line ? `SWITCHUSER user=${who}\r\n` : "authentication failed\r\n";
        } else {
          g.commands.push(line);
          g.out += `${line}\r\n`;
          const m = /^switchuser (\S+)$/.exec(line);
          if (m) { g.prompt = m[1]; g.out += "Password:"; }
        }
        flush();
      };
      g.wait = (re, from) => new Promise(resolve => {
        const timer = setTimeout(() => resolve(false), 30);
        const check = () => re.test(g.out.slice(from)) ? (clearTimeout(timer), resolve(true)) : g.waiters.push(check);
        check();
      });
      return g;
    };
    const run = async (g, input, steps, onWait) => {
      const statuses = [];
      let waits = 0;
      const ok = await driveGuestSteps(steps, null, {
        input, ready: () => true, write: v => g.write(v), mark: () => g.out.length,
        wait: (re, from) => { if (waits++ === 0 && onWait) onWait(); return g.wait(re, from); },
        said: from => g.out.slice(from), status: m => statuses.push(m),
      });
      await new Promise(r => setTimeout(r, 40));
      return { ok, statuses };
    };
    const pw = drawPw();
    const dnsack = "dnsack example.lab 10.0.0.5";
    {
      const g = echoGuest({ Ada: pw });
      const r = await run(g, createGuestInput(), guestSwitchLines("Ada", pw, "Flinstone"));
      assert.strictEqual(r.ok, true);
      assert.deepStrictEqual(g.answers, { Ada: pw });
      assert.deepStrictEqual(g.commands, ["switchuser Ada"]);
    }
    {
      const g = echoGuest({ Ada: pw });
      const input = createGuestInput();
      const r = await run(g, input, guestSwitchLines("Ada", pw, "Flinstone"),
        () => { void input.send(() => g.write(`${dnsack}\n`)); });
      assert.strictEqual(r.ok, false);
      assert.strictEqual(g.answers.Ada, dnsack, "the dnsack answered the prompt (the race happened)");
      assert(!g.commands.includes(pw), "the password must not be typed as a command");
      assert(/Other input reached the guest/.test(r.statuses.join("\n")));
    }
    {
      const g = echoGuest({ Ada: pw });
      const input = createGuestInput();
      const unguarded = { send: w => input.send(w), sendIfLast: (_mark, w) => input.send(w) };
      await run(g, unguarded, guestSwitchLines("Ada", pw, "Flinstone"),
        () => { void input.send(() => g.write(`${dnsack}\n`)); });
      assert(g.commands.includes(pw), "control: without the guard the password becomes a command");
    }
    {
      const rootPw = drawPw();
      const g = echoGuest({ Ada: pw, Root: rootPw }, "Ada");
      const r = await run(g, createGuestInput(), guestSwitchLines("Root", rootPw, "Flinstone"));
      assert.strictEqual(r.ok, false);
      assert.strictEqual(g.answers.Ada, "switchuser Root");
      assert(!g.commands.includes(rootPw), "a late prompt must not draw the password out as a command");
      assert(/did not ask for a password/.test(r.statuses.join("\n")));
    }
    assert.strictEqual(promptAfterEcho("switchuser Root\r\nPassword:", "switchuser Root"), true);
    assert.strictEqual(promptAfterEcho("Password:***************\r\n", "switchuser Root"), false);
    assert.strictEqual(promptAfterEcho("Password:switchuser Root\r\n", "switchuser Root"), false);
    {
      const input = createGuestInput();
      const first = await input.send(() => {});
      assert.strictEqual(await input.sendIfLast(first, () => {}), first + 1);
      await input.send(() => {});
      assert.strictEqual(await input.sendIfLast(first + 1, () => assert.fail("must not send")), 0);
    }
    assert(labJs.includes("const guestKeys = core.createGuestInput();"), "every guest input is counted");
    assert(/core\.driveGuestSteps\(steps, patterns, \{\s*input: guestKeys,/.test(labJs), "exchanges send through the counted input");
    assert(!/emulator\.(sendText|typeChar|sendKey)\(/.test(labJs.replace(/enqueueGuest\(\(\) => emulator\.\w+\([^)]*\)\)/g, "").replace("write: value => emulator.sendText(value)", "")),
      "nothing reaches the guest except through guestKeys");
  }
  console.log("test_browser_lab: PASS");
})().catch((error) => { console.error(error); process.exitCode = 1; });
