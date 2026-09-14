(() => {
  "use strict";
  const core = window.FlintstoneLabCore;
  const config = Object.assign({
    metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",
    parentOrigin: "https://bailey-forbes.com",
    createEmulator: window.createFlintstoneQemu,
  }, window.FLINTSTONE_LAB_CONFIG || {});
  const text = (id, value) => { document.getElementById(id).textContent = value; };
  const setState = (state, detail) => {
    const node = document.getElementById("status");
    node.className = [core.STATES.BLOCKED, core.STATES.FAILED].includes(state) ? "blocked" : "";
    node.textContent = detail ? `${state}: ${detail}` : state;
    document.documentElement.dataset.labState = String(state).toLowerCase().replace(/\s+/g, "-");
  };
  let info;
  let busy = false;
  const validation = new URLSearchParams(location.search).get("validate") === "1";
  const serial = document.getElementById("serial");
  const canvas = document.querySelector("#screen canvas");
  function renderScreen(bytes) {
    // Render the guest-owned 80x25 VGA text buffer at physical 0xb8000.
    // This is a text-mode display, not a graphics-mode VGA implementation.
    if (bytes.length !== 4000) return;
    const colors = ["#000", "#00a", "#0a0", "#0aa", "#a00", "#a0a", "#a50", "#aaa", "#555", "#55f", "#5f5", "#5ff", "#f55", "#f5f", "#ff5", "#fff"];
    canvas.width = 800; canvas.height = 400;
    const ctx = canvas.getContext("2d");
    ctx.font = "16px monospace"; ctx.textBaseline = "top";
    for (let i = 0; i < 2000; i++) {
      const ch = bytes[i * 2], attr = bytes[i * 2 + 1], x = (i % 80) * 10, y = Math.floor(i / 80) * 16;
      ctx.fillStyle = colors[(attr >> 4) & 7]; ctx.fillRect(x, y, 10, 16);
      ctx.fillStyle = colors[attr & 15];
      if (ch >= 32 && ch <= 126) ctx.fillText(String.fromCharCode(ch), x, y);
    }
    document.getElementById("display-placeholder").hidden = true;
  }
  const options = () => ({
    artifactUrl: new URL(`${config.artifactBaseUrl}/${info.artifact}?v=${info.shortCommit}`, location.href).href,
    memorySize: info.recommendedRamBytes,
    sha256: info.sha256,
    onScreen: renderScreen, onDiagnostic: text => console.warn(text),
  });
  const controller = core.createController({
    marker: "FLINTSTONE_KERNEL_BOOT_OK", setState,
    postReady: () => window.parent.postMessage({ source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: info.shortCommit }, config.parentOrigin),
    createEmulator: async (settings) => {
      if (typeof config.createEmulator !== "function") throw new Error("No validated x86-64 browser emulator is configured");
      serial.textContent = "";
      return config.createEmulator({ ...settings, serialByte: byte => {
        serial.textContent = (serial.textContent + String.fromCharCode(byte)).slice(-65536);
        settings.serialByte(byte);
      } });
    },
  });
  async function load() {
    setState(core.STATES.LOADING);
    const response = await fetch(config.metadataUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Metadata request failed: HTTP ${response.status}`);
    info = core.validateManifest(await response.json());
    text("commit", info.shortCommit); text("architecture", info.architecture);
    text("emulator", info.browserEmulator); text("artifact", info.artifact);
    if (!canBoot()) {
      setState(core.STATES.BLOCKED, info.blockers.join("; "));
      return;
    }
    await controller.boot(options());
  }
  function canBoot() { return info && info.bootable && (info.browserCompatible || (validation && info.bootableCandidate)); }
  for (const [id, action] of Object.entries({ boot: () => controller.boot(options()), pause: () => controller.pause(), resume: () => controller.resume(), reset: () => controller.reset(options()), poweroff: () => controller.powerOff() })) {
    document.getElementById(id).onclick = async () => {
      if (busy || ((id === "boot" || id === "reset") && !canBoot())) return;
      busy = true;
      try { await action(); } catch (error) { controller.fail(error); } finally { busy = false; }
    };
  }
  load().catch((error) => controller.fail(error));
})();
