#!/usr/bin/env node
"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { createRequire } = require("node:module");
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
  const child = spawn(python, [path.join(root, "scripts/serve_browser_lab.py"), ...args], { stdio: ["ignore", "pipe", "pipe"] });
  servers.push(child);
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("server startup timed out: " + args.join(" "))), 15000);
    child.once("error", reject);
    child.once("exit", code => reject(new Error(`server exited: ${code}`)));
    child.stdout.on("data", data => { if (String(data).includes("Browser lab:")) { clearTimeout(timer); resolve(); } });
  });
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
  await frame.locator("#display-placeholder").waitFor({ state: "hidden", timeout: 20000 });
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
  await frame.getByRole("button", { name: "Reset", exact: true }).click();
  await frame.locator("#status").filter({ hasText: /^Ready$/ }).waitFor({ timeout: 90000 });
  await frame.getByRole("button", { name: "Power Off", exact: true }).click();
  await frame.locator("#status").filter({ hasText: /^Powered off$/ }).waitFor();
  await frame.getByRole("button", { name: "Boot", exact: true }).click();
  await waitReady(frame);
  await frame.locator("#account-new-session").click();
  await frame.locator("#serial").filter({ hasText: /SESSION 2 user=root/ }).waitFor({ timeout: 20000 });
  assert(errors.length === 0, `iframe errors: ${errors.join("; ")}`);
  await page.screenshot({ path: path.join(root, "dist/browser-iframe.png"), fullPage: true });

  const swParentDir = fs.mkdtempSync(path.join(os.tmpdir(), "flintstone-iframe-sw-parent-"));
  const swLabOrigin = `http://127.0.0.1:${swLabPort}`;
  const swParentOrigin = `http://127.0.0.2:${swParentPort}`;
  fs.writeFileSync(path.join(swParentDir, "index.html"), parentHtml(swLabOrigin, manifest.shortCommit));
  await serve(["--bind", "127.0.0.1", "--port", String(swLabPort), "--directory", labRoot,
    "--no-coop-coep", "--corp", "cross-origin", "--frame-ancestors", swParentOrigin]);
  await serve(["--bind", "127.0.0.2", "--port", String(swParentPort), "--directory", swParentDir,
    "--permissions-policy", `cross-origin-isolated=(self "${swLabOrigin}")`, "--corp", "cross-origin"]);
  const fresh = await browser.newContext({ viewport: { width: 1200, height: 1100 } });
  await fresh.addInitScript({ content: `window.FLINTSTONE_LAB_CONFIG = { parentOrigin: ${JSON.stringify(swParentOrigin)} };` });
  const swPage = await fresh.newPage();
  await swPage.goto(swParentOrigin + "/", { waitUntil: "domcontentloaded" });
  await waitReady(swPage.frameLocator("#lab"));
  await swPage.waitForFunction(() => window.trustedReady === true, null, { timeout: 20000 });
  await fresh.close();
  console.log("test-browser-iframe: PASS (parent COOP/COEP, headered child, first-visit SW child, ready origin/source/schema/commit)");
}
main().catch(error => { console.error(error); process.exitCode = 1; }).finally(async () => {
  if (browser) await browser.close();
  for (const child of servers) child.kill();
});
