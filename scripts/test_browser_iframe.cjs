#!/usr/bin/env node
"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { createRequire } = require("node:module");
const { spawnLabServer, finishBrowserLabTest } = require("./lib/browser_lab_process.cjs");
const root = path.resolve(__dirname, "..");
const { chromium } = createRequire(path.join(root, "tools/browser-lab/package.json"))("@playwright/test");
const labRoot = path.join(root, "dist/browser-lab");
const manifest = JSON.parse(fs.readFileSync(path.join(labRoot, "artifacts/build-info.json")));
const assert = (condition, text) => { if (!condition) throw new Error(text); };
const labPort = Number(process.env.FL_IFRAME_LAB_PORT || 8771);
const parentPort = Number(process.env.FL_IFRAME_PARENT_PORT || 8772);
const swLabPort = Number(process.env.FL_IFRAME_SW_LAB_PORT || 8773);
const swParentPort = Number(process.env.FL_IFRAME_SW_PARENT_PORT || 8774);
const python = process.env.FL_PYTHON || "python3";
const servers = [];
let browser;

function serve(args) {
  const launched = spawnLabServer(python, [path.join(root, "scripts/serve_browser_lab.py"), ...args]);
  servers.push(launched.child);
  return launched.ready;
}

function parentHtml(labOrigin, commit) {
  return `<!doctype html>
<title>Portfolio parent</title>
<iframe id="lab" allow="cross-origin-isolated" src="${labOrigin}/" title="Flintstone Kernel Lab" style="width:1100px;height:1000px;border:0"></iframe>
<script>
window.trustedReady = false;
window.addEventListener("message", event => {
  const frame = document.getElementById("lab");
  const data = event.data;
  window.trustedReady = Boolean(
    event.origin === ${JSON.stringify(labOrigin)} &&
    event.source === frame.contentWindow &&
    data && data.source === "flinstone-guest" && data.type === "ready" &&
    data.schemaVersion === 1 && data.commit === ${JSON.stringify(commit)}
  );
});
</script>
`;
}

async function waitReady(frame) {
  await frame.locator("#status").filter({ hasText: /^Ready$/ }).waitFor({ timeout: 120000 });
  const serial = await frame.locator("#serial").innerText();
  assert(serial.split(/\r?\n/).includes("FLINTSTONE_KERNEL_BOOT_OK"), "iframe missing exact serial marker");
  await frame.locator("#display-placeholder").waitFor({ state: "hidden", timeout: 45000 });
  await frame.locator(":root[data-vga-cell='F']").waitFor({ timeout: 20000 });
  assert(await frame.locator(":root").getAttribute("data-vga-cell") === "F", "iframe VGA cell was not F/0x07");
}

async function main() {
  assert(manifest.bootable === true && manifest.browserCompatible === true, "Package a validated lab first");
  const parentDir = fs.mkdtempSync(path.join(os.tmpdir(), "flintstone-iframe-parent-"));
  const labOrigin = `http://127.0.0.1:${labPort}`;
  const parentOrigin = `http://127.0.0.2:${parentPort}`;
  fs.writeFileSync(path.join(parentDir, "index.html"), parentHtml(labOrigin, manifest.shortCommit));
  await serve(["--bind", "127.0.0.1", "--port", String(labPort), "--directory", labRoot,
    "--corp", "cross-origin", "--frame-ancestors", parentOrigin]);
  await serve(["--bind", "127.0.0.2", "--port", String(parentPort), "--directory", parentDir,
    "--permissions-policy", `cross-origin-isolated=(self "${labOrigin}")`, "--corp", "cross-origin"]);
  browser = await chromium.launch({ headless: true, ...(process.env.FL_BROWSER_CHANNEL ? { channel: process.env.FL_BROWSER_CHANNEL } : {}) });
  const context = await browser.newContext({ viewport: { width: 1200, height: 1100 } });
  await context.addInitScript({ content: `window.FLINTSTONE_LAB_CONFIG = { parentOrigin: ${JSON.stringify(parentOrigin)} };` });
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  await page.goto(parentOrigin + "/", { waitUntil: "domcontentloaded" });
  const frame = page.frameLocator("#lab");
  await waitReady(frame);
  await page.waitForFunction(() => window.trustedReady === true, null, { timeout: 20000 });
  await frame.getByRole("button", { name: "Pause", exact: true }).click();
  await frame.locator("#status").filter({ hasText: /^Paused$/ }).waitFor();
  await frame.getByRole("button", { name: "Resume", exact: true }).click();
  await frame.locator("#status").filter({ hasText: /^Ready$/ }).waitFor();
  await frame.locator("#account-new-session").click();
  try {
    await frame.locator("#session-tabs [data-session='2']").filter({ hasText: /Session 2: root/ }).waitFor({ timeout: 20000 });
    await frame.locator("#account-status").filter({ hasText: /Active session 2: root/ }).waitFor({ timeout: 5000 });
  } catch (error) {
    throw new Error(`iframe new session: ${await frame.locator("#serial").innerText()}\n${error}`);
  }
  assert(errors.length === 0, `iframe errors: ${errors.join("; ")}`);
  await page.screenshot({ path: path.join(root, "dist/browser-iframe.png"), fullPage: true });
  await frame.getByRole("button", { name: "Power Off", exact: true }).click();
  await frame.locator("#status").filter({ hasText: /^Powered off$/ }).waitFor();
  await page.close();

  const swParentDir = fs.mkdtempSync(path.join(os.tmpdir(), "flintstone-iframe-sw-parent-"));
  const swLabOrigin = `http://127.0.0.1:${swLabPort}`;
  const swParentOrigin = `http://127.0.0.2:${swParentPort}`;
  fs.writeFileSync(path.join(swParentDir, "index.html"), parentHtml(swLabOrigin, manifest.shortCommit));
  await serve(["--bind", "127.0.0.1", "--port", String(swLabPort), "--directory", labRoot,
    "--no-coop-coep", "--corp", "cross-origin", "--frame-ancestors", swParentOrigin]);
  await serve(["--bind", "127.0.0.2", "--port", String(swParentPort), "--directory", swParentDir,
    "--coep-credentialless",
    "--permissions-policy", `cross-origin-isolated=(self "${swLabOrigin}")`, "--corp", "cross-origin"]);
  const fresh = await browser.newContext({ viewport: { width: 1200, height: 1100 } });
  await fresh.addInitScript({ content: `window.FLINTSTONE_LAB_CONFIG = { parentOrigin: ${JSON.stringify(swParentOrigin)} };` });
  // A COEP parent blocks the first headerless document (ERR_BLOCKED_BY_RESPONSE),
  // so the service worker never installs inside a first-visit iframe. GitHub Pages
  // therefore warms isolation with a top-level visit; the worker then injects COEP
  // on the subsequent framed navigation.
  const top = await fresh.newPage();
  await top.goto(swLabOrigin + "/", { waitUntil: "domcontentloaded" });
  await waitReady(top);
  await top.getByRole("button", { name: "Power Off", exact: true }).click();
  await top.locator("#status").filter({ hasText: /^Powered off$/ }).waitFor();
  await top.close();
  const swPage = await fresh.newPage();
  await swPage.goto(swParentOrigin + "/", { waitUntil: "domcontentloaded" });
  await waitReady(swPage.frameLocator("#lab"));
  await swPage.waitForFunction(() => window.trustedReady === true, null, { timeout: 20000 });
  await fresh.close();
  console.log("test-browser-iframe: PASS (parent COOP/COEP, headered child, top-level first-visit SW then iframe, ready origin/source/schema/commit)");
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(() =>
  finishBrowserLabTest({ browser, servers }));
