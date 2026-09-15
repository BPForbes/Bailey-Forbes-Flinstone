(() => {
  "use strict";
  const scriptBase = new URL(".", document.currentScript.src);
  const shifted = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8",
    "(": "9", ")": "0", "_": "minus", "+": "equal", "{": "bracket_left", "}": "bracket_right",
    ":": "semicolon", "\"": "apostrophe", "~": "grave_accent", "|": "backslash",
    "<": "comma", ">": "dot", "?": "slash",
  };
  const plain = {
    " ": "spc", "\n": "ret", "\r": "ret", "\t": "tab", "-": "minus", "=": "equal",
    "[": "bracket_left", "]": "bracket_right", ";": "semicolon", "'": "apostrophe",
    "`": "grave_accent", "\\": "backslash", ",": "comma", ".": "dot", "/": "slash",
  };
  function qcodesForChar(ch) {
    if (ch >= "a" && ch <= "z") return [ch];
    if (ch >= "A" && ch <= "Z") return ["shift", ch.toLowerCase()];
    if (ch >= "0" && ch <= "9") return [ch];
    if (shifted[ch]) return ["shift", shifted[ch]];
    if (plain[ch]) return [plain[ch]];
    return null;
  }
  function qcodesForEvent(event) {
    const extra = [];
    if (event.ctrlKey) extra.push("ctrl");
    if (event.altKey) extra.push("alt");
    if (event.metaKey) extra.push("meta_l");
    if (event.key === "Backspace") return extra.concat("backspace");
    if (event.key === "Enter") return extra.concat("ret");
    if (event.key === "Tab") return extra.concat("tab");
    if (event.key === "Escape") return extra.concat("esc");
    if (event.key === " ") return extra.concat(event.shiftKey ? ["shift", "spc"] : ["spc"]);
    const codes = qcodesForChar(event.key);
    if (!codes) return extra.length ? extra : null;
    if (event.shiftKey && codes[0] !== "shift") return extra.concat("shift", ...codes);
    return extra.concat(codes);
  }
  window.FlintstoneQemuKeys = { qcodesForChar, qcodesForEvent };
  window.createFlintstoneQemu = async ({ artifactUrl, sha256, memorySize, serialByte, onError, onDiagnostic, onScreen }) => {
    if (!crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      throw new Error("Browser boot requires cross-origin isolation. Use the lab server or configure COOP/COEP headers.");
    }
    if (!window.FlintstoneQmp || typeof window.FlintstoneQmp.createQmpClient !== "function") {
      throw new Error("QMP client is not loaded");
    }
    const response = await fetch(artifactUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Disk download failed: HTTP ${response.status}`);
    const image = await response.arrayBuffer();
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", image)), x => x.toString(16).padStart(2, "0")).join("");
    if (hash !== sha256) throw new Error("Disk SHA-256 does not match its manifest");
    const worker = new Worker(new URL("qemu-worker.js", scriptBase), { type: "module" });
    let disposed = false, screenTimer, screenBusy = false, holdScreen = 0, inputHold = 0, inputHoldTimer = 0;
    const qmp = window.FlintstoneQmp.createQmpClient({
      send: data => worker.postMessage({ type: "qmp", data }),
      timeoutMs: 30000,
    });
    function command(execute, args, timeoutMs) {
      return qmp.command(execute, args, timeoutMs);
    }
    async function sendKey(qcodes) {
      if (!qcodes || !qcodes.length) return;
      await command("send-key", { keys: qcodes.map(data => ({ type: "qcode", data })) });
    }
    async function withScreenHeld(fn) {
      holdScreen += 1;
      try { return await fn(); }
      finally { holdScreen -= 1; }
    }
    function pulseInputHold() {
      inputHold = 1;
      if (inputHoldTimer) clearTimeout(inputHoldTimer);
      inputHoldTimer = setTimeout(() => { inputHold = 0; inputHoldTimer = 0; }, 250);
    }
    worker.onmessage = async ({ data }) => {
      if (disposed) return;
      if (data.type === "serial") serialByte(data.byte);
      if (data.type === "diagnostic") onDiagnostic?.(data.text);
      if (data.type === "error") { screenBusy = false; onError?.(new Error(data.text)); }
      if (data.type === "screen") { screenBusy = false; onScreen?.(data.bytes); }
      if (data.type !== "qmp") return;
      if (qmp.accept(data.data) !== "greeting") return;
      try {
        await command("qmp_capabilities");
        qmp.allowWork();
        screenTimer = setInterval(async () => {
          if (disposed || screenBusy || holdScreen || inputHold) return;
          screenBusy = true;
          try {
            await command("pmemsave", { val: 753664, size: 4000, filename: "/screen.bin" }, 3000);
            if (!disposed) worker.postMessage({ type: "screen" });
            else screenBusy = false;
          } catch (error) { screenBusy = false; onDiagnostic?.(error.message); }
        }, 250);
      } catch (error) { onError?.(error); }
    };
    worker.onerror = event => onError?.(new Error(event.message || "QEMU worker failed"));
    worker.postMessage({ type: "boot", image, memorySize }, [image]);
    return {
      async stop() {
        await withScreenHeld(async () => {
          const s = await command("query-status");
          if (s && !s.running) return;
          await command("stop");
          const after = await command("query-status");
          if (after.running) throw new Error("QEMU did not pause");
        });
      },
      async run() {
        await withScreenHeld(async () => {
          const s = await command("query-status");
          if (s && s.running) return;
          await command("cont");
          const after = await command("query-status");
          if (!after.running) throw new Error("QEMU did not resume");
        });
      },
      async sendKey(qcodes) {
        pulseInputHold();
        try { await withScreenHeld(() => sendKey(qcodes)); }
        finally { pulseInputHold(); }
      },
      async sendText(text) {
        pulseInputHold();
        try {
          await withScreenHeld(async () => {
            for (const ch of text) {
              const codes = qcodesForChar(ch);
              if (!codes) continue;
              await sendKey(codes);
              // Yield so the guest can drain the 8042 between QMP send-key bursts.
              await new Promise(resolve => setTimeout(resolve, ch === "\n" || ch === "\r" ? 80 : 20));
            }
          });
        } finally { pulseInputHold(); }
      },
      async destroy() {
        disposed = true; holdScreen += 1; inputHold = 1;
        if (inputHoldTimer) clearTimeout(inputHoldTimer);
        clearInterval(screenTimer);
        qmp.failAll(new Error("QEMU powered off"));
        await new Promise(resolve => {
          const timeout = setTimeout(resolve, 1000);
          worker.onmessage = ({ data }) => { if (data.type === "destroyed") { clearTimeout(timeout); resolve(); } };
          worker.postMessage({ type: "destroy" });
        });
        worker.terminate();
      },
    };
  };
})();
