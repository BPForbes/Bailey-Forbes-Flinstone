#!/usr/bin/env node
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const { createRequire } = require("node:module");
const { spawnLabServer, finishBrowserLabTest } = require("./lib/browser_lab_process.cjs");
const root = path.resolve(__dirname, "..");
const { chromium } = createRequire(path.join(root, "tools/browser-lab/package.json"))("@playwright/test");
const digest = data => crypto.createHash("sha256").update(data).digest("hex");
const assert = (condition, text) => { if (!condition) throw new Error(text); };
const packaged = process.env.FL_BROWSER_TEST_PACKAGED === "1";
const labRoot = packaged ? path.join(root, "dist/browser-lab") : root;
const labPath = packaged ? "/" : "/tools/browser-lab/";
const manifestPath = packaged
  ? path.join(labRoot, "artifacts/build-info.json")
  : path.join(root, "dist/build-info.json");
const diskPath = packaged
  ? path.join(labRoot, "artifacts/flintstone.img")
  : path.join(root, "dist/flintstone.img");
const manifest = JSON.parse(fs.readFileSync(manifestPath));
const lock = JSON.parse(fs.readFileSync(path.join(root, "tools/browser-lab/runtime-lock.json")));
const port = Number(process.env.FL_BROWSER_TEST_PORT || (packaged ? 8770 : 8768));
const relayPort = Number(process.env.FL_BROWSER_TEST_RELAY_PORT || (packaged ? 8775 : 8767));
const bind = process.env.FL_BROWSER_TEST_BIND || "127.0.0.1";
const base = `http://${bind}:${port}`;
let server, browser;

async function main() {
  assert(manifest.bootable === true, "Run the independent native QEMU probe first");
  assert(manifest.artifact === "flintstone.img", "Unexpected artifact path");
  assert(digest(fs.readFileSync(diskPath)) === manifest.sha256, "Disk digest mismatch");
  const runtimeDir = packaged
    ? path.join(labRoot, "vendor/qemu")
    : path.join(root, "tools/browser-lab/vendor/qemu");
  for (const [name, sha] of Object.entries(lock.files)) {
    assert(digest(fs.readFileSync(path.join(runtimeDir, name))) === sha, `Runtime digest mismatch: ${name}`);
  }
  const serverArgs = [path.join(root, "scripts/serve_browser_lab.py"), "--port", String(port), "--bind", bind, "--directory", labRoot, "--relay-port", String(relayPort)];
  const launched = spawnLabServer(process.env.FL_PYTHON || (process.platform === "win32" ? "python" : "python3"), serverArgs);
  server = launched.child;
  await launched.ready;
  browser = await chromium.launch({ headless: true, ...(process.env.FL_BROWSER_CHANNEL ? { channel: process.env.FL_BROWSER_CHANNEL } : {}) });
  const page = await browser.newPage({ viewport: { width: 1100, height: 1100 } });
  await page.addInitScript(port => {
    window.FLINTSTONE_LAB_CONFIG = Object.assign({}, window.FLINTSTONE_LAB_CONFIG || {}, { relayPort: port });
  }, relayPort);
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  const status = async value => page.locator("#status").filter({ hasText: new RegExp(`^${value}$`) }).waitFor({ timeout: 90000 });
  const guestText = async () => {
    const term = (await page.locator("#wasm-term").textContent().catch(() => "")) || "";
    const serial = (await page.locator("#serial").textContent().catch(() => "")) || "";
    return `${term}\n${serial}`;
  };
  const waitGuest = async (re, timeout = 20000) => {
    const source = re instanceof RegExp ? re.source : String(re);
    await page.waitForFunction(src => {
      const rx = new RegExp(src);
      const term = document.getElementById("wasm-term");
      const serial = document.getElementById("serial");
      return rx.test(`${term ? term.textContent : ""}\n${serial ? serial.textContent : ""}`);
    }, source, { timeout });
  };
  const ready = async () => {
    await status("Ready");
    const serial = await guestText();
    assert(serial.split(/\r?\n/).includes("FLINTSTONE_KERNEL_BOOT_OK"), "Missing exact serial marker");
    await page.locator("#display-placeholder").waitFor({ state: "hidden", timeout: 45000 });
    await page.locator(":root[data-vga-cell='F']").waitFor({ timeout: 20000 });
    assert(await page.locator(":root").getAttribute("data-vga-cell") === "F", "VGA diagnostic cell was not F/0x07");
    return serial;
  };

  if (!packaged) {
    // An unvalidated manifest cannot boot via the ordinary page or its Boot button.
    await page.route("**/build-info.json", route => route.fulfill({ json: { ...manifest, browserCompatible: false } }));
    await page.goto(`${base}${labPath}?qemu=1`);
    await page.locator("#status").filter({ hasText: /^Blocked/ }).waitFor();
    await page.getByRole("button", { name: "Boot", exact: true }).click();
    assert((await page.locator("#status").innerText()).startsWith("Blocked"), "Boot bypassed compatibility gate");
    assert(page.workers().length === 0, "Blocked lab created a worker");
    await page.unroute("**/build-info.json");

    // Corrupt downloads must fail before an emulator is created.
    await page.route("**/flintstone.img?*", route => route.fulfill({ body: Buffer.from("corrupt disk") }));
    await page.goto(`${base}${labPath}?validate=1`);
    await page.locator("#status").filter({ hasText: /Failed: Disk SHA-256/ }).waitFor();
    assert(page.workers().length === 0, "Corrupt disk created an emulator");
    await page.unroute("**/flintstone.img?*");
  }

  await page.goto(`${base}${labPath}${packaged ? "" : "?validate=1"}`);
  const serial = await ready();
  if (!packaged) {
    for (let i = 0; i < 2; i++) {
      await page.getByRole("button", { name: "Pause", exact: true }).click(); await status("Paused");
      await page.getByRole("button", { name: "Resume", exact: true }).click(); await status("Ready");
    }
    await page.getByRole("button", { name: "Reset", exact: true }).click();
    await status("Booting"); await ready();
    await page.getByRole("button", { name: "Power Off", exact: true }).click(); await status("Powered off");
    for (let i = 0; i < 100 && page.workers().length; i++) await new Promise(resolve => setTimeout(resolve, 50)); assert(page.workers().length === 0, "Power Off left workers alive: " + page.workers().map(worker => worker.url()).join(", "));
    await page.getByRole("button", { name: "Boot", exact: true }).click(); await ready();
  }
  await page.locator("#screen").click();
  await page.keyboard.type("dir\n", { delay: 40 });
  await waitGuest(/readme.txt/);
  await page.keyboard.type("write hello.txt lab-fs\n", { delay: 40 });
  await waitGuest(/wrote hello.txt/, 30000);
  await page.keyboard.type("cat hello.txt\n", { delay: 40 });
  await waitGuest(/lab-fs/);
  await page.keyboard.type("whoami\n", { delay: 40 });
  await waitGuest(/WHOAMI flinstone/);
  await page.locator("#account-name").fill("root");
  await page.getByRole("button", { name: "Switch user", exact: true }).click();
  await page.locator("#account-status").filter({ hasText: /Active session 1: root/ }).waitFor({ timeout: 20000 });
  await page.locator("#account-new-session").click();
  await page.locator("#session-tabs [data-session='2']").filter({ hasText: /Session 2: root/ }).waitFor({ timeout: 20000 });
  await page.locator("#account-status").filter({ hasText: /Active session 2: root/ }).waitFor({ timeout: 5000 });
  const sessionOne = page.locator("#session-tabs [data-session='1']");
  await sessionOne.waitFor({ state: "visible", timeout: 5000 });
  await sessionOne.click();
  try {
    await page.locator("#account-status").filter({ hasText: /Active session 1: root/ }).waitFor({ timeout: 20000 });
    await waitGuest(/SESSION 1 user=root/);
  } catch (error) {
    throw new Error(`session 1 switch: ${await guestText()}\n${error}`);
  }
  await page.locator("#server-panel").waitFor({ state: "visible", timeout: 5000 });
  await page.getByRole("button", { name: "Host", exact: true }).click();
  try {
    await page.locator("#server-status").filter({ hasText: /Connected as/ }).waitFor({ timeout: 15000 });
  } catch (error) {
    throw new Error(`relay connect failed: ${await page.locator("#server-status").innerText()}`);
  }
  await page.locator("#server-msg").fill("hello relay");
  await page.locator("#server-msg-form").getByRole("button", { name: "Send" }).click();
  await page.locator("#server-chat").filter({ hasText: /hello relay/ }).waitFor({ timeout: 10000 });
  await page.locator("#guest-cmd").fill("server msg from-guest");
  await page.getByRole("button", { name: "Run", exact: true }).click();
  await waitGuest(/SERVER_RELAY msg from-guest/, 30000);
  await page.locator("#server-chat").filter({ hasText: /from-guest/ }).waitFor({ timeout: 10000 });
  assert(errors.length === 0, `Browser errors: ${errors.join("; ")}`);
  await page.screenshot({ path: path.join(root, packaged ? "dist/browser-boot-packaged.png" : "dist/browser-boot.png"), fullPage: true });
  const wasmJs = packaged
    ? path.join(labRoot, "wasm/flintstone.js")
    : path.join(root, "tools/browser-lab/wasm/flintstone.js");
  if (!packaged && fs.existsSync(wasmJs)) {
    await page.getByRole("button", { name: "Power Off", exact: true }).click();
    await status("Powered off");
    await page.goto(`${base}${labPath}`);
    await ready();
    await page.locator("#wasm-term").waitFor({ timeout: 10000 });
    await waitGuest(/shell>/);
    await page.locator("#screen").click();
    await page.keyboard.type("whoami\n");
    await waitGuest(/WHOAMI flinstone/);
    await page.locator("#account-name").fill("root");
    await page.getByRole("button", { name: "Switch user", exact: true }).click();
    await page.locator("#account-status").filter({ hasText: /Active session 1: root/ }).waitFor({ timeout: 20000 });
  }
  await page.getByRole("button", { name: "Power Off", exact: true }).click();
  await status("Powered off");
  if (packaged) {
    console.log("test-browser-boot: PASS packaged (rewritten assets, serial marker, VGA cell, switch-user sessions, relay)");
    return;
  }
  const evidence = {
    commit: manifest.commit, artifactSha256: manifest.sha256,
    runtime: lock.runtime, runtimeCommit: lock.distributionCommit, runtimeFiles: lock.files,
    testedAt: new Date().toISOString(), browser: browser.version(),
    serial, checks: ["exact-marker", "vga-text-memory", "pause-resume-twice", "reset", "power-off-worker-cleanup", "reboot", "blocked-button", "corrupt-digest", "switchuser-perspectives", "server-relay-chat", "lab-ramfs", "guest-server-relay"],
  };
  const current = JSON.parse(fs.readFileSync(manifestPath));
  assert(current.commit === manifest.commit && current.sha256 === manifest.sha256, "Manifest changed during validation");
  assert(digest(fs.readFileSync(diskPath)) === manifest.sha256, "Disk changed during validation");
  current.browserEmulator = "QEMU Wasm x86_64 (b7c549b5e6f4)";
  current.browserCompatible = true;
  current.validationOutcome = "browser-bootable";
  current.blockers = current.blockers.filter(value => !/^(Browser has not|An independently tested x86-64 browser)/.test(value));
  assert(current.blockers.length === 0, "Unresolved artifact blockers prevent promotion");
  current.browserValidation = evidence;
  fs.writeFileSync(path.join(root, "dist/browser-validation.json"), JSON.stringify(evidence, null, 2) + "\n");
  fs.writeFileSync(manifestPath, JSON.stringify(current, null, 2) + "\n");
  console.log("test-browser-boot: PASS (real QEMU Wasm, serial marker, VGA cell, lifecycle, negative gates, switch-user)");
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(() =>
  finishBrowserLabTest({ browser, servers: [server] }));
