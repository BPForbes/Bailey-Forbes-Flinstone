(() => {
  "use strict";

  const config = Object.assign({
    metadataUrl: "../../dist/build-info.json",
    artifactBaseUrl: "../../dist",
    v86ScriptUrl: "./vendor/libv86.js",
    v86WasmUrl: "./vendor/v86.wasm",
    biosUrl: "./vendor/seabios.bin",
    vgaBiosUrl: "./vendor/vgabios.bin",
  }, window.FLINTSTONE_LAB_CONFIG || {});

  const text = (id, value) => {
    document.getElementById(id).textContent = value;
  };

  const loadScript = (url) => new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = url;
    script.onload = resolve;
    script.onerror = () => reject(new Error(`Unable to load emulator runtime: ${url}`));
    document.head.appendChild(script);
  });

  async function start() {
    const response = await fetch(config.metadataUrl, { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`Metadata request failed: HTTP ${response.status}`);
    }
    const info = await response.json();

    text("commit", info.shortCommit);
    text("architecture", info.architecture);
    text("emulator", info.browserEmulator);
    text("artifact", info.artifact);

    if (!info.bootable || !info.v86Compatible) {
      const reason = (info.blockers || []).join("; ");
      const status = document.getElementById("status");
      status.className = "blocked";
      status.textContent = `Blocked by architecture contract: ${reason}`;
      return;
    }

    if (!window.V86) {
      await loadScript(config.v86ScriptUrl);
    }
    if (!window.V86) {
      throw new Error("The configured emulator runtime did not expose window.V86");
    }

    const artifactUrl = new URL(`${config.artifactBaseUrl}/${info.artifact}`, location.href);
    artifactUrl.searchParams.set("v", info.shortCommit);
    window.flintstoneEmulator = new window.V86({
      wasm_path: config.v86WasmUrl,
      screen_container: document.getElementById("screen"),
      bios: { url: config.biosUrl },
      vga_bios: { url: config.vgaBiosUrl },
      hda: { url: artifactUrl.href },
      memory_size: info.recommendedRamBytes,
      autostart: true,
    });
    text("status", "Booting");
  }

  start().catch((error) => {
    const status = document.getElementById("status");
    status.className = "blocked";
    status.textContent = error.message;
  });
})();
