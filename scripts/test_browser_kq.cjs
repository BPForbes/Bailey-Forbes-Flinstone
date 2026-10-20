#!/usr/bin/env node
"use strict";
/*
 * Files between two lab pages in a real browser, through the server panel and the local relay
 * hub: a KeyQuorum co-signed file (send form, the recipient's Co-sign button, every checkpoint
 * in both checkpoint logs, the delivered file in the recipient's shell) and a plain file (send
 * form, Accept button). Needs the WebAssembly lab built with KeyQuorum (EMCC_REQUIRED=1 make
 * wasm) and Playwright's Chromium.
 */
const fs = require("node:fs");
const path = require("node:path");
const { createRequire } = require("node:module");
const { spawnLabServer, finishBrowserLabTest } = require("./lib/browser_lab_process.cjs");

const root = path.resolve(__dirname, "..");
const { chromium } = createRequire(path.join(root, "tools/browser-lab/package.json"))("@playwright/test");
const assert = (condition, text) => { if (!condition) throw new Error(text); };
const port = Number(process.env.FL_BROWSER_TEST_PORT || 8791);
const relayPort = Number(process.env.FL_BROWSER_TEST_RELAY_PORT || 8792);
const base = `http://127.0.0.1:${port}/tools/browser-lab/`;
let server, browser;

async function openLab(name) {
  const page = await browser.newPage({ viewport: { width: 1200, height: 1600 } });
  await page.addInitScript(config => {
    window.FLINTSTONE_LAB_CONFIG = Object.assign({}, window.FLINTSTONE_LAB_CONFIG || {}, config);
  }, { relayPort, relayRoom: "kq-test" });
  const errors = [];
  page.on("pageerror", error => errors.push(error.message));
  page.errors = errors;
  await page.goto(base);
  await page.locator("#status").filter({ hasText: /^Ready$/ }).waitFor({ timeout: 90000 });
  // A visitor registers their own user (built-in accounts publish no keys) and switches to it.
  await page.fill("#account-name", name);
  await page.fill("#account-secret", name);
  await page.click("#account-register");
  await page.waitForFunction(n => (document.getElementById("wasm-term")?.textContent || "").includes(`useradd ${n}`),
                             name, { timeout: 20000 });
  await page.click("#account-login");
  await page.waitForFunction(n => (document.getElementById("server-status")?.textContent || "").includes(`Connected as ${n}`),
                             name, { timeout: 30000 });
  return page;
}

async function shell(page, command) {
  await page.fill("#guest-cmd", command);
  await page.click("#guest-cmd-run");
  await page.waitForFunction(c => (document.getElementById("wasm-term")?.textContent || "").includes(c), command,
                             { timeout: 20000 });
}

async function main() {
  assert(fs.existsSync(path.join(root, "tools/browser-lab/wasm/flintstone.wasm")),
         "Build the WebAssembly lab first: EMCC_REQUIRED=1 make wasm");
  const launched = spawnLabServer(process.env.FL_PYTHON || "python3", [
    path.join(root, "scripts/serve_browser_lab.py"), "--port", String(port), "--bind", "127.0.0.1",
    "--directory", root, "--relay-port", String(relayPort)]);
  server = launched.child;
  await launched.ready;
  browser = await chromium.launch({ headless: true });
  const alice = await openLab("alice"); // first on the server: its host
  const bob = await openLab("bob");
  await alice.waitForFunction(() => /alice \(host\)[\s\S]*bob/.test(document.getElementById("server-roster")?.textContent || ""),
                              null, { timeout: 20000 });
  const kqAvailable = await alice.evaluate(() => Boolean(document.getElementById("kq-log")));
  assert(kqAvailable, "server panel has no KeyQuorum controls");

  // KeyQuorum: encode (alice) -> host -> request (bob) -> co-sign (bob) -> release -> decode (bob).
  await shell(alice, "write note.txt hello bob");
  await alice.fill("#server-file-path", "note.txt");
  await alice.fill("#server-file-user", "bob");
  await alice.check("#server-file-kq");
  await alice.click("#server-file-form button[type=submit]");
  await bob.locator("#kq-requests li button").first().waitFor({ timeout: 20000 });
  assert(/note\.txt from alice/.test(await bob.textContent("#kq-requests")), "bob is not asked to co-sign");
  await shell(bob, "cat note.txt");
  assert(!/\nhello bob/.test(await bob.textContent("#wasm-term")), "the file arrived before bob co-signed");
  await bob.click("#kq-requests li button");
  await bob.waitForFunction(() => /decode: ok/.test(document.getElementById("kq-log")?.textContent || ""), null,
                            { timeout: 20000 });
  const aliceLog = await alice.textContent("#kq-log");
  const bobLog = await bob.textContent("#kq-log");
  assert(/encode: ok/.test(aliceLog), "alice's encode checkpoint is not logged");
  for (const step of [/request: alice asks you to co-sign note\.txt/, /co-sign: ok/, /decode: ok/, /saved: note\.txt/])
    assert(step.test(bobLog), `bob's log lacks ${step}`);
  assert(!/refused/.test(aliceLog + bobLog), "a checkpoint refused the delivery");
  await shell(bob, "cat note.txt");
  await bob.waitForFunction(() => /\nhello bob/.test(document.getElementById("wasm-term")?.textContent || ""), null,
                            { timeout: 10000 });

  // A plain file through the same form, accepted with the panel's button.
  await shell(alice, "write plain.txt just text");
  await alice.fill("#server-file-path", "plain.txt");
  await alice.uncheck("#server-file-kq");
  await alice.click("#server-file-form button[type=submit]");
  await bob.waitForFunction(() => /alice sent plain\.txt/.test(document.getElementById("server-chat")?.textContent || ""),
                            null, { timeout: 20000 });
  await bob.click("#server-file-accept");
  await bob.waitForFunction(() => /received plain\.txt from alice/.test(document.getElementById("wasm-term")?.textContent || ""),
                            null, { timeout: 20000 });

  assert(alice.errors.length === 0 && bob.errors.length === 0, `page errors: ${[...alice.errors, ...bob.errors].join("; ")}`);
  console.log("test-browser-kq: PASS (KeyQuorum co-signed and plain files between two lab pages)");
}

main().catch(error => { console.error(error); process.exitCode = 1; }).finally(() =>
  finishBrowserLabTest({ browser, servers: [server] }));
