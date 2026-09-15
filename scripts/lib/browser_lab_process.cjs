"use strict";
const { spawn } = require("node:child_process");

function spawnLabServer(python, args, { timeoutMs = 15000 } = {}) {
  const child = spawn(python, args, {
    stdio: ["ignore", "pipe", "pipe"],
    detached: process.platform !== "win32",
  });
  const ready = new Promise((resolve, reject) => {
    let stderr = "";
    child.stderr.on("data", chunk => { stderr += String(chunk); });
    const timer = setTimeout(() => reject(new Error("Lab server startup timed out: " + args.join(" "))), timeoutMs);
    child.once("error", reject);
    const onExit = code => {
      clearTimeout(timer);
      const detail = stderr.trim() ? `\n${stderr.trim()}` : "";
      reject(new Error(`Lab server exited: ${code}${detail}`));
    };
    child.once("exit", onExit);
    child.stdout.on("data", data => {
      if (String(data).includes("Browser lab:")) {
        child.removeListener("exit", onExit);
        clearTimeout(timer);
        resolve();
      }
    });
  });
  return { child, ready };
}

function killProcessTree(child) {
  if (!child || child.killed || child.pid == null) return;
  try {
    if (process.platform !== "win32") {
      process.kill(-child.pid, "SIGKILL");
    } else {
      child.kill();
    }
  } catch (_) {
    try { child.kill("SIGKILL"); } catch (_) { /* already gone */ }
  }
}

async function finishBrowserLabTest({ browser, servers, timeoutMs = 4000 }) {
  const force = setTimeout(() => process.exit(process.exitCode ?? 0), timeoutMs);
  try {
    if (browser) {
      await Promise.race([
        browser.close(),
        new Promise(resolve => setTimeout(resolve, Math.max(1000, timeoutMs - 1000))),
      ]);
    }
  } catch (_) {
    /* close can hang on a live QEMU Wasm worker; process.exit follows */
  }
  for (const child of servers) killProcessTree(child);
  clearTimeout(force);
  process.exit(process.exitCode ?? 0);
}

module.exports = { spawnLabServer, killProcessTree, finishBrowserLabTest };
