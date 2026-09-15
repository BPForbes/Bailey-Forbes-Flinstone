"use strict";
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const net = require("node:net");
const { spawn } = require("node:child_process");
const { spawnLabServer, killProcessTree } = require("../scripts/lib/browser_lab_process.cjs");

const root = path.resolve(__dirname, "..");
const python = process.env.FL_PYTHON || "python3";
const port = Number(process.env.FL_PROCESS_TEST_PORT || 8791);
const relayPort = Number(process.env.FL_PROCESS_TEST_RELAY_PORT || 8792);

function waitExit(child, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    if (child.exitCode != null) {
      resolve(child.exitCode);
      return;
    }
    const timer = setTimeout(() => reject(new Error("child did not exit")), timeoutMs);
    child.once("exit", code => {
      clearTimeout(timer);
      resolve(code);
    });
  });
}

function portOpen(openPort) {
  return new Promise(resolve => {
    const socket = net.connect({ host: "127.0.0.1", port: openPort }, () => {
      socket.end();
      resolve(true);
    });
    socket.on("error", () => resolve(false));
  });
}

async function testKillTreeStopsRelay() {
  const launched = spawnLabServer(python, [
    path.join(root, "scripts/serve_browser_lab.py"),
    "--port", String(port), "--bind", "127.0.0.1",
    "--directory", root, "--relay-port", String(relayPort),
  ]);
  try {
    await launched.ready;
    const listenDeadline = Date.now() + 5000;
    while (Date.now() < listenDeadline && !(await portOpen(relayPort))) {
      await new Promise(r => setTimeout(r, 50));
    }
    if (!(await portOpen(relayPort))) throw new Error("relay did not listen");
    killProcessTree(launched.child);
    await waitExit(launched.child);
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      if (!(await portOpen(relayPort))) return;
      await new Promise(r => setTimeout(r, 50));
    }
    throw new Error("relay port still open after killProcessTree");
  } finally {
    killProcessTree(launched.child);
  }
}

async function testRelayStartsWhenServingPackagedTree() {
  const served = fs.mkdtempSync(path.join(os.tmpdir(), "flintstone-packaged-serve-"));
  const packagedPort = Number(process.env.FL_PROCESS_TEST_PACKAGED_PORT || 8793);
  const packagedRelay = Number(process.env.FL_PROCESS_TEST_PACKAGED_RELAY_PORT || 8794);
  const launched = spawnLabServer(python, [
    path.join(root, "scripts/serve_browser_lab.py"),
    "--port", String(packagedPort), "--bind", "127.0.0.1",
    "--directory", served, "--relay-port", String(packagedRelay),
  ]);
  try {
    await launched.ready;
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline && !(await portOpen(packagedRelay))) {
      await new Promise(r => setTimeout(r, 50));
    }
    if (!(await portOpen(packagedRelay))) throw new Error("packaged-directory relay did not listen");
  } finally {
    killProcessTree(launched.child);
    fs.rmSync(served, { recursive: true, force: true });
  }
}

function testFinishExitsDespiteHungClose() {
  return new Promise((resolve, reject) => {
    const child = spawn(process.execPath, ["-e", `
      const { finishBrowserLabTest } = require(${JSON.stringify(path.join(root, "scripts/lib/browser_lab_process.cjs"))});
      finishBrowserLabTest({
        browser: { close: () => new Promise(() => {}) },
        servers: [],
        timeoutMs: 400,
      });
    `], { stdio: "ignore" });
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject(new Error("finishBrowserLabTest did not exit while browser.close hung"));
    }, 2000);
    child.once("exit", code => {
      clearTimeout(timer);
      if (code === 0) resolve();
      else reject(new Error("finishBrowserLabTest exited " + code));
    });
  });
}

(async () => {
  await testKillTreeStopsRelay();
  await testRelayStartsWhenServingPackagedTree();
  await testFinishExitsDespiteHungClose();
  console.log("test_browser_lab_process: PASS");
})().catch(error => {
  console.error(error);
  process.exit(1);
});
