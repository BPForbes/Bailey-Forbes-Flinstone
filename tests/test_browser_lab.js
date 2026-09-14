"use strict";
const assert = require("assert");
const fs = require("fs");
const vm = require("vm");
const { STATES, validateManifest, isTrustedReadyEvent, createController, validDiagnosticVga } = require("../tools/browser-lab/lab-core.js");
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
assert(!coiSrc.includes("Sec-Fetch-Dest"), "SW cannot read Sec-Fetch-Dest; it is a forbidden header");
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
  const states = []; let ready = 0; let created = 0; let destroyed = 0;
  const emulator = { stop() {}, run() {}, async destroy() { destroyed++; } };
  const controller = createController({ marker: manifest.bootSuccessMarker, setState: (s) => states.push(s), postReady: () => ready++, createEmulator: async ({ serialByte }) => { created++; emulator.serialByte = serialByte; return emulator; } });
  await controller.boot({});
  "premature FLINTSTONE_KERNEL_BOOT_OK suffix\n".split("").forEach(emulator.serialByte);
  assert.strictEqual(ready, 0);
  "FLINTSTONE_KERNEL_BOOT_OK\r\n".split("").forEach(emulator.serialByte);
  assert.strictEqual(ready, 1); assert.strictEqual(states.at(-1), STATES.READY);
  assert(await controller.pause()); assert.strictEqual(states.at(-1), STATES.PAUSED);
  assert(await controller.resume()); assert.strictEqual(states.at(-1), STATES.READY);
  await controller.reset({}); assert.strictEqual(created, 2); assert.strictEqual(destroyed, 1); assert.strictEqual(states.at(-1), STATES.BOOTING);
  await controller.powerOff(); assert.strictEqual(states.at(-1), STATES.OFF);
  const failing = createController({ marker: "x", setState: (s) => states.push(s), postReady() {}, createEmulator: async () => { throw new Error("corrupt image"); } });
  await assert.rejects(() => failing.boot({}), /corrupt image/); assert.strictEqual(states.at(-1), STATES.FAILED);
  console.log("test_browser_lab: PASS");
})().catch((error) => { console.error(error); process.exitCode = 1; });
