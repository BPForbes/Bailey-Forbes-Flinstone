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

  window.createFlintstoneWasm = async ({ serialByte, onError, onDiagnostic, onScreen, onKqFrame, onKqEvent }) => {
    const factory = await loadFactory();
    let disposed = false;
    let paused = false;
    const term = document.getElementById("wasm-term");
    // The guest streams raw output one byte at a time, and that stream can
    // contain multi-byte UTF-8 sequences (e.g. an em dash is 3 bytes).
    // String.fromCharCode(byte) treats each byte as its own Latin-1 code
    // point, which mangles any such sequence into mojibake. A streaming
    // TextDecoder buffers an incomplete sequence across calls and only
    // yields output once a full code point has arrived; for the ASCII
    // bytes that make up the overwhelming majority of guest output it
    // still returns that same single character immediately, so this adds
    // no delay there.
    const shellDecoder = new TextDecoder("utf-8", { fatal: false });
    let started = null;
    const mod = await factory({
      locateFile: name => new URL(name, wasmDir).href,
      // The native KeyQuorum host and client report with printf: show them in the terminal.
      print: text => {
        if (!started) return;
        for (const byte of new TextEncoder().encode(`${text}\n`)) started.onShellChar(byte);
      },
      printErr: text => onDiagnostic?.(String(text)),
    });
    started = mod;
    mod.onKqFrame = frame => { if (!disposed) onKqFrame?.(frame); };
    mod.onKqEvent = event => { if (!disposed) onKqEvent?.(event); };
    mod.onShellChar = code => {
      if (disposed) return;
      const ch = shellDecoder.decode(new Uint8Array([code]), { stream: true });
      if (!ch) { serialByte(code); return; }
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
    function withBytes(bytes, fn) {
      const at = mod._malloc(bytes.length || 1);
      mod.HEAPU8.set(bytes, at);
      try { return fn(at); } finally { mod._free(at); }
    }
    function withString(value, fn) {
      const at = mod.stringToNewUTF8(String(value));
      try { return fn(at); } finally { mod._free(at); }
    }
    // KeyQuorum co-signed delivery, when the module was built with it (lab_kq.c).
    const kq = typeof mod._wasm_kq_frame !== "function" ? {} : {
      kqFrame(toHost, memberId, op, bytes) {
        if (disposed) return;
        withBytes(bytes, at => mod._wasm_kq_frame(toHost ? 1 : 0, memberId, op, at, bytes.length));
        snapshotVga();
      },
      kqRoster(selfId, members) {
        const bytes = [];
        for (const m of members || []) {
          const principal = new TextEncoder().encode(String(m.principal || "")).slice(0, 63);
          if (!principal.length) continue;
          bytes.push((m.memberId >> 8) & 0xff, m.memberId & 0xff, m.isHost ? 1 : 0, principal.length, ...principal);
        }
        withBytes(Uint8Array.from(bytes), at => mod._wasm_kq_roster(selfId || 0, at, bytes.length));
      },
      kqLearn(principal, keys) {
        return withString(principal, name => withBytes(keys, at => mod._wasm_kq_learn(name, at, at + 32))) === 1;
      },
      kqPublic(principal) {
        return withString(principal, name => {
          const at = mod._wasm_kq_public(name);
          return at ? mod.HEAPU8.slice(at, at + 64) : null;
        });
      },
      kqTick() {
        if (!disposed) mod._wasm_kq_tick();
      },
    };
    return {
      ...kq,
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
