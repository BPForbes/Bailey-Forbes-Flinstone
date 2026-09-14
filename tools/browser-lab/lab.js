(() => {
  "use strict";
  const core = window.FlintstoneLabCore;
  const allowedParents = origin => {
    if (origin === "https://bailey-forbes.com") return true;
    try {
      const url = new URL(origin);
      return url.protocol === "http:" && (url.hostname === "127.0.0.1" || url.hostname === "127.0.0.2");
    } catch (_) {
      return false;
    }
  };
  const config = Object.assign({
    metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",
    parentOrigin: "https://bailey-forbes.com",
    createEmulator: window.createFlintstoneQemu,
  }, window.FLINTSTONE_LAB_CONFIG || {});
  if (!allowedParents(config.parentOrigin)) config.parentOrigin = "https://bailey-forbes.com";
  const text = (id, value) => { const node = document.getElementById(id); if (node) node.textContent = value; };
  const resetDisplayProbe = () => {
    document.documentElement.dataset.vgaCell = "";
    const placeholder = document.getElementById("display-placeholder");
    if (placeholder) placeholder.hidden = false;
  };
  const setState = (state, detail) => {
    const node = document.getElementById("status");
    node.className = [core.STATES.BLOCKED, core.STATES.FAILED].includes(state) ? "blocked" : "";
    node.textContent = detail ? `${state}: ${detail}` : state;
    document.documentElement.dataset.labState = String(state).toLowerCase().replace(/\s+/g, "-");
    if (state === core.STATES.BOOTING || state === core.STATES.OFF) resetDisplayProbe();
  };
  let info;
  let busy = false;
  let emulator = null;
  const sessions = { 1: "flinstone" };
  let activeSession = 1;
  const validation = new URLSearchParams(location.search).get("validate") === "1";
  const serial = document.getElementById("serial");
  const canvas = document.querySelector("#screen canvas");
  const screen = document.getElementById("screen");
  function renderCaps(capabilities) {
    const node = document.getElementById("capabilities");
    if (!node) return;
    const rows = [
      ["identity", "Identity / switch user"],
      ["hostedLabSessions", "Multiple sessions"],
      ["keyboard", "Keyboard"],
      ["filesystem", "Filesystem"],
      ["network", "Networking"],
      ["server", "Server host/join"],
    ];
    node.replaceChildren();
    for (const [key, label] of rows) {
      const item = document.createElement("div");
      const on = Boolean(capabilities && capabilities[key]);
      item.className = on ? "cap-on" : "cap-off";
      item.textContent = `${label}: ${on ? "available" : "unavailable"}`;
      node.appendChild(item);
    }
  }
  function renderSessions() {
    const node = document.getElementById("session-tabs");
    if (!node) return;
    node.replaceChildren();
    Object.keys(sessions).sort((a, b) => Number(a) - Number(b)).forEach(id => {
      const button = document.createElement("button");
      button.type = "button";
      button.dataset.session = id;
      button.textContent = `Session ${id}: ${sessions[id]}`;
      if (Number(id) === activeSession) button.setAttribute("aria-current", "true");
      button.onclick = () => sendGuest(`session ${id}\n`);
      node.appendChild(button);
    });
    text("account-status", `Active session ${activeSession}: ${sessions[activeSession] || "—"}`);
  }
  function noteSerial(value) {
    const match = /SESSION (\d+) user=(\S+)/.exec(value);
    if (match) {
      activeSession = Number(match[1]);
      sessions[activeSession] = match[2];
      renderSessions();
    }
  }
  function renderScreen(bytes) {
    // Render the guest-owned 80x25 VGA text buffer at physical 0xb8000.
    // This is a text-mode display, not a graphics-mode VGA implementation.
    // Latch the diagnostic cell: SeaBIOS/empty dumps must not clear a later
    // kernel frame, and they must not hide the placeholder on a fresh boot.
    if (!core.validDiagnosticVga(bytes)) return;
    document.documentElement.dataset.vgaCell = "F";
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
  async function sendGuest(value) {
    if (!emulator || typeof emulator.sendText !== "function") return;
    await emulator.sendText(value);
  }
  const controller = core.createController({
    marker: "FLINTSTONE_KERNEL_BOOT_OK", setState,
    postReady: () => window.parent.postMessage({ source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: info.shortCommit }, config.parentOrigin),
    createEmulator: async (settings) => {
      if (typeof config.createEmulator !== "function") throw new Error("No validated x86-64 browser emulator is configured");
      serial.textContent = "";
      emulator = await config.createEmulator({ ...settings, serialByte: byte => {
        serial.textContent = (serial.textContent + String.fromCharCode(byte)).slice(-65536);
        noteSerial(serial.textContent.slice(-80));
        settings.serialByte(byte);
      } });
      return emulator;
    },
  });
  async function load() {
    setState(core.STATES.LOADING);
    const response = await fetch(config.metadataUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Metadata request failed: HTTP ${response.status}`);
    info = core.validateManifest(await response.json());
    text("commit", info.shortCommit); text("architecture", info.architecture);
    text("emulator", info.browserEmulator); text("artifact", info.artifact);
    renderCaps(info.capabilities);
    renderSessions();
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
  screen.addEventListener("keydown", event => {
    if (!emulator || typeof emulator.sendKey !== "function") return;
    const codes = window.FlintstoneQemuKeys && window.FlintstoneQemuKeys.qcodesForEvent(event);
    if (!codes) return;
    event.preventDefault();
    emulator.sendKey(codes).catch(error => controller.fail(error));
  });
  const form = document.getElementById("switch-user");
  if (form) {
    form.addEventListener("submit", async event => {
      event.preventDefault();
      const name = document.getElementById("account-name").value.trim();
      const password = document.getElementById("account-password").value;
      if (!name) return;
      await sendGuest(`login ${name}\n${password}\n`);
    });
    document.getElementById("account-new-session").onclick = async () => {
      const name = document.getElementById("account-name").value.trim() || "flinstone";
      const password = document.getElementById("account-password").value || name;
      await sendGuest(`session new\nlogin ${name}\n${password}\n`);
    };
  }
  load().catch((error) => controller.fail(error));
})();
