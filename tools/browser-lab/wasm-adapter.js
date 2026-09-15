(() => {
  "use strict";
  const scriptBase = document.currentScript ? new URL(".", document.currentScript.src) : new URL("./", location.href);
  const wasmDir = new URL("wasm/", scriptBase);

  function appendTerm(node, ch) {
    if (!node) return;
    if (ch === "\b") {
      node.textContent = node.textContent.slice(0, -1);
      return;
    }
    node.textContent += ch;
    node.scrollTop = node.scrollHeight;
  }

  function loadFactory() {
    if (typeof window.createFlintstoneShell === "function")
      return Promise.resolve(window.createFlintstoneShell);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = new URL("flintstone.js", wasmDir).href;
      script.onload = () => {
        if (typeof window.createFlintstoneShell === "function")
          resolve(window.createFlintstoneShell);
        else
          reject(new Error("WebAssembly factory createFlintstoneShell is missing"));
      };
      script.onerror = () => reject(new Error("WebAssembly lab module is missing. Run make wasm."));
      document.head.appendChild(script);
    });
  }

  window.createFlintstoneWasm = async ({ serialByte, onError, onDiagnostic, onScreen }) => {
    const factory = await loadFactory();
    let disposed = false;
    let paused = false;
    const term = document.getElementById("wasm-term");
    const mod = await factory({
      locateFile: name => new URL(name, wasmDir).href,
      printErr: text => onDiagnostic?.(String(text)),
    });
    mod.onShellChar = code => {
      if (disposed) return;
      const ch = String.fromCharCode(code);
      appendTerm(term, ch);
      serialByte(code);
    };
    function snapshotVga() {
      if (disposed || typeof onScreen !== "function" || typeof mod._wasm_vga_buffer !== "function")
        return;
      const ptr = mod._wasm_vga_buffer();
      const nbytes = typeof mod._wasm_vga_bytes === "function" ? mod._wasm_vga_bytes() : 4000;
      onScreen(new Uint8Array(mod.HEAPU8.buffer, ptr, nbytes).slice());
    }
    function typeChar(ch) {
      if (disposed || paused || !ch) return;
      if (ch === "\r")
        ch = "\n";
      if (ch === "\n" || ch === "\b" || (ch.length === 1 && ch.charCodeAt(0) >= 32 && ch.charCodeAt(0) <= 126))
        mod._wasm_shell_type(ch.charCodeAt(0));
      snapshotVga();
    }
    try {
      mod._wasm_shell_init();
      snapshotVga();
    } catch (error) {
      onError?.(error);
      throw error;
    }
    return {
      async stop() { paused = true; },
      async run() { paused = false; },
      async sendKey() {},
      async sendText(text) {
        if (disposed || paused) return;
        for (const ch of String(text || ""))
          typeChar(ch);
      },
      typeChar,
      async destroy() {
        disposed = true;
        paused = true;
      },
    };
  };
})();
