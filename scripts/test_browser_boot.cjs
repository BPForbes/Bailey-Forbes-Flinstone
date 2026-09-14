#!/usr/bin/env node
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { spawn } = require("node:child_process");
const { createRequire } = require("node:module");
const root = path.resolve(__dirname, "..");
const { chromium } = createRequire(path.join(root, "tools/browser-lab/package.json"))("@playwright/test");
const digest = data => crypto.createHash("sha256").update(data).digest("hex");
const assert = (condition, text) => { if (!condition) throw new Error(text); };
const manifestPath = path.join(root, "dist/build-info.json");
const manifest = JSON.parse(fs.readFileSync(manifestPath));
const lock = JSON.parse(fs.readFileSync(path.join(root, "tools/browser-lab/runtime-lock.json")));
const port = Number(process.env.FL_BROWSER_TEST_PORT || 8768);
const base = `http://127.0.0.1:${port}`;
let server, browser;

async function main() {
  assert(manifest.bootable === true, "Run the independent native QEMU probe first");
  assert(manifest.artifact === "flintstone.img", "Unexpected artifact path");
  assert(digest(fs.readFileSync(path.join(root, "dist", manifest.artifact))) === manifest.sha256, "Disk digest mismatch");
  for (const [name, sha] of Object.entries(lock.files)) {
    assert(digest(fs.readFileSync(path.join(root, "tools/browser-lab/vendor/qemu", name))) === sha, `Runtime digest mismatch: ${name}`);
  }
  server = spawn(process.env.FL_PYTHON || (process.platform === "win32" ? "python" : "python3"),
    [path.join(root, "scripts/serve_browser_lab.py"), "--port", String(port)], { stdio: ["ignore", "pipe", "pipe"] });
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("Lab server startup timed out")), 15000);
    server.once("error", reject);
    server.once("exit", code => reject(new Error(`Lab server exited: ${code}`)));
    server.stdout.on("data", data => { if (String(data).includes("Browser lab:")) { clearTimeout(timer); resolve(); } });
  });
  browser = await chromium.launch({ headless: true, ...(process.env.FL_BROWSER_CHANNEL ? { channel: process.env.FL_BROWSER_CHANNEL } : {}) });
  const page = await browser.newPage({ viewport: { width: 1100, height: 1000 } });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  const status = async value => page.locator("#status").filter({ hasText: new RegExp(`^${value}$`) }).waitFor({ timeout: 90000 });
  const ready = async () => {
    await status("Ready");
    const serial = await page.locator("#serial").innerText();
    assert(serial.split(/\r?\n/).includes("FLINTSTONE_KERNEL_BOOT_OK"), "Missing exact serial marker");
    await page.locator("#display-placeholder").waitFor({ state: "hidden", timeout: 20000 });
    return serial;
  };

  // An unvalidated manifest cannot boot via the ordinary page or its Boot button.
  await page.route("**/build-info.json", route => route.fulfill({ json: { ...manifest, browserCompatible: false } }));
  await page.goto(`${base}/tools/browser-lab/`);
  await page.locator("#status").filter({ hasText: /^Blocked/ }).waitFor();
  await page.getByRole("button", { name: "Boot", exact: true }).click();
  assert((await page.locator("#status").innerText()).startsWith("Blocked"), "Boot bypassed compatibility gate");
  assert(page.workers().length === 0, "Blocked lab created a worker");
  await page.unroute("**/build-info.json");

  // Corrupt downloads must fail before an emulator is created.
  await page.route("**/flintstone.img?*", route => route.fulfill({ body: Buffer.from("corrupt disk") }));
  await page.goto(`${base}/tools/browser-lab/?validate=1`);
  await page.locator("#status").filter({ hasText: /Failed: Disk SHA-256/ }).waitFor();
  assert(page.workers().length === 0, "Corrupt disk created an emulator");
  await page.unroute("**/flintstone.img?*");

  await page.goto(`${base}/tools/browser-lab/?validate=1`);
  const serial = await ready();
  for (let i = 0; i < 2; i++) {
    await page.getByRole("button", { name: "Pause", exact: true }).click(); await status("Paused");
    await page.getByRole("button", { name: "Resume", exact: true }).click(); await status("Ready");
  }
  await page.getByRole("button", { name: "Reset", exact: true }).click();
  await status("Booting"); await ready();
  await page.getByRole("button", { name: "Power Off", exact: true }).click(); await status("Powered off");
  for (let i = 0; i < 100 && page.workers().length; i++) await new Promise(resolve => setTimeout(resolve, 50)); assert(page.workers().length === 0, "Power Off left workers alive: " + page.workers().map(worker => worker.url()).join(", "));
  await page.getByRole("button", { name: "Boot", exact: true }).click(); await ready();
  assert(errors.length === 0, `Browser errors: ${errors.join("; ")}`);
  await page.screenshot({ path: path.join(root, "dist/browser-boot.png"), fullPage: true });
  const evidence = {
    commit: manifest.commit, artifactSha256: manifest.sha256,
    runtime: lock.runtime, runtimeCommit: lock.distributionCommit, runtimeFiles: lock.files,
    testedAt: new Date().toISOString(), browser: browser.version(),
    serial, checks: ["exact-marker", "vga-text-memory", "pause-resume-twice", "reset", "power-off-worker-cleanup", "reboot", "blocked-button", "corrupt-digest"],
  };
  // Stamp only after every real-browser and negative check passes, and only if
  // no concurrent build replaced the manifest or disk while the test ran.
  const current = JSON.parse(fs.readFileSync(manifestPath));
  assert(current.commit === manifest.commit && current.sha256 === manifest.sha256, "Manifest changed during validation");
  assert(digest(fs.readFileSync(path.join(root, "dist/flintstone.img"))) === manifest.sha256, "Disk changed during validation");
  current.browserEmulator = "QEMU Wasm x86_64 (b7c549b5e6f4)";
  current.browserCompatible = true;
  current.validationOutcome = "browser-bootable";
  current.blockers = current.blockers.filter(value => !/^(Browser has not|An independently tested x86-64 browser)/.test(value));
  assert(current.blockers.length === 0, "Unresolved artifact blockers prevent promotion");
  current.browserValidation = evidence;
  fs.writeFileSync(path.join(root, "dist/browser-validation.json"), JSON.stringify(evidence, null, 2) + "\n");
  fs.writeFileSync(manifestPath, JSON.stringify(current, null, 2) + "\n");
  console.log("test-browser-boot: PASS (real QEMU Wasm, serial marker, VGA, lifecycle, negative gates)");
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  if (server) server.kill();
});
