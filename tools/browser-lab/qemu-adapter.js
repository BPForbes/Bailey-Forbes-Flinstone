(() => {
  "use strict";
  const scriptBase = new URL(".", document.currentScript.src);
  window.createFlintstoneQemu = async ({ artifactUrl, sha256, memorySize, serialByte, onError, onDiagnostic, onScreen }) => {
    if (!crossOriginIsolated || typeof SharedArrayBuffer === "undefined") {
      throw new Error("Browser boot requires cross-origin isolation. Use the lab server or configure COOP/COEP headers.");
    }
    const response = await fetch(artifactUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Disk download failed: HTTP ${response.status}`);
    const image = await response.arrayBuffer();
    const hash = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", image)), x => x.toString(16).padStart(2, "0")).join("");
    if (hash !== sha256) throw new Error("Disk SHA-256 does not match its manifest");
    const worker = new Worker(new URL("qemu-worker.js", scriptBase), { type: "module" });
    let nextId = 0, disposed = false, screenTimer, pending = new Map();
    function command(execute, args) {
      return new Promise((resolve, reject) => {
        const id = ++nextId;
        const timeout = setTimeout(() => { pending.delete(id); reject(new Error(`QEMU command timed out: ${execute}`)); }, 15000);
        pending.set(id, { resolve, reject, timeout });
        worker.postMessage({ type: "qmp", data: { execute, ...(args ? { arguments: args } : {}), id } });
      });
    }
    worker.onmessage = async ({ data }) => {
      if (disposed) return;
      if (data.type === "serial") serialByte(data.byte);
      if (data.type === "diagnostic") onDiagnostic?.(data.text);
      if (data.type === "error") onError?.(new Error(data.text));
      if (data.type === "screen") onScreen?.(data.bytes);
      if (data.type !== "qmp") return;
      if (data.data.QMP) {
        try {
          await command("qmp_capabilities");
          screenTimer = setInterval(async () => {
            if (disposed) return;
            try {
              await command("pmemsave", { val: 753664, size: 4000, filename: "/screen.bin" });
              if (!disposed) worker.postMessage({ type: "screen" });
            } catch (error) { onDiagnostic?.(error.message); }
          }, 1000);
        } catch (error) { onError?.(error); }
      }
      const result = pending.get(data.data.id);
      if (result) {
        clearTimeout(result.timeout); pending.delete(data.data.id);
        if (data.data.error) result.reject(new Error(data.data.error.desc)); else result.resolve(data.data.return);
      }
    };
    worker.onerror = event => onError?.(new Error(event.message || "QEMU worker failed"));
    worker.postMessage({ type: "boot", image, memorySize }, [image]);
    return {
      async stop() { await command("stop"); const s = await command("query-status"); if (s.running) throw new Error("QEMU did not pause"); },
      async run() { await command("cont"); const s = await command("query-status"); if (!s.running) throw new Error("QEMU did not resume"); },
      async destroy() {
        disposed = true; clearInterval(screenTimer);
        for (const p of pending.values()) { clearTimeout(p.timeout); p.reject(new Error("QEMU powered off")); }
        pending.clear();
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
