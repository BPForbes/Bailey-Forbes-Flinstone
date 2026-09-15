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
    relayPort: 8767,
    relayRoom: "lab",
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
    const pauseBtn = document.getElementById("pause");
    const resumeBtn = document.getElementById("resume");
    if (pauseBtn) pauseBtn.disabled = state !== core.STATES.READY;
    if (resumeBtn) resumeBtn.disabled = state !== core.STATES.PAUSED;
    if (state === core.STATES.BOOTING || state === core.STATES.OFF) resetDisplayProbe();
    const ready = state === core.STATES.READY;
    const cmd = document.getElementById("guest-cmd");
    const run = document.getElementById("guest-cmd-run");
    if (cmd) cmd.disabled = !ready;
    if (run) run.disabled = !ready;
    if (ready) screen.focus();
  };
  let info;
  let busy = false;
  let emulator = null;
  let extraUsers = [];
  let sessions = { 1: "flinstone" };
  let activeSession = 1;
  let relay = null;
  let serialLine = "";
  const validation = new URLSearchParams(location.search).get("validate") === "1";
  const usesBrowserRelay = () => info && info.serverPath === "relay";
  const principal = () => sessions[activeSession] || "flinstone";
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
      let on = Boolean(capabilities && capabilities[key]);
      let detail = on ? "available" : "unavailable";
      if (key === "server" && usesBrowserRelay()) {
        on = true;
        detail = "relay (browser-hosted)";
      } else if (key === "network") {
        detail = on ? "lab analog + DNS" : "unavailable";
      }
      item.className = on ? "cap-on" : "cap-off";
      item.textContent = `${label}: ${detail}`;
      node.appendChild(item);
    }
  }
  function renderRuntimeMode() {
    const node = document.getElementById("runtime-mode");
    if (!node || !info) return;
    if (info.runtimeMode === "browser-hosted" || usesBrowserRelay()) {
      node.textContent = "Runtime: browser-hosted online — server chat via JS relay (same wire as net_server.c). Local VM/bare-metal uses native C/ASM.";
    } else {
      node.textContent = "Runtime: native local — use server host/join in the shell (kernel/core/net).";
    }
  }
  function appendChat(line) {
    const node = document.getElementById("server-chat");
    if (!node) return;
    node.textContent = (node.textContent + line + "\n").slice(-65536);
    node.scrollTop = node.scrollHeight;
  }
  function renderRelayStatus(text) {
    const node = document.getElementById("server-status");
    if (node) node.textContent = text;
  }
  function renderRoster(members) {
    const node = document.getElementById("server-roster");
    if (!node) return;
    if (!members || !members.length) {
      node.textContent = "Members: —";
      return;
    }
    node.textContent = members.map(m => `#${m.memberId} ${m.principal}${m.isHost ? " (host)" : ""}`).join("\n");
  }
  function setupRelay() {
    const panel = document.getElementById("server-panel");
    if (!usesBrowserRelay() || !window.FlintstoneServerRelayClient) {
      if (panel) panel.hidden = true;
      return;
    }
    if (panel) panel.hidden = false;
    relay = window.FlintstoneServerRelayClient.createRelayClient(config);
    relay.on(event => {
      if (event.type === "hello") {
        renderRelayStatus(`Connected as ${event.display} (#${event.memberId})`);
        appendChat(`[relay] joined as ${event.display}`);
      } else if (event.type === "announcement") {
        appendChat(`[announce] ${event.text}`);
      } else if (event.type === "message") {
        appendChat(event.text);
      } else if (event.type === "roster") {
        renderRoster(event.members);
      } else if (event.type === "error") {
        appendChat(`[error] ${event.text}`);
      } else if (event.type === "closed") {
        renderRelayStatus("Disconnected");
      }
    });
    const connect = async () => {
      try {
        renderRelayStatus("Connecting…");
        await relay.connect(principal());
      } catch (error) {
        renderRelayStatus(error.message || "Connection failed");
      }
    };
    document.getElementById("server-host")?.addEventListener("click", connect);
    document.getElementById("server-join")?.addEventListener("click", connect);
    document.getElementById("server-leave")?.addEventListener("click", () => {
      relay?.leave();
      renderRelayStatus("Disconnected");
      renderRoster([]);
    });
    document.getElementById("server-msg-form")?.addEventListener("submit", async event => {
      event.preventDefault();
      const input = document.getElementById("server-msg");
      const text = input?.value.trim();
      if (!text) return;
      if (!relay?.connected) await connect();
      if (relay?.sendMessage(text)) {
        appendChat(`${relay.display}: ${text}`);
        input.value = "";
      }
    });
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
      button.onclick = async () => { await sendGuest(`session ${id}\n`); };
      node.appendChild(button);
    });
    text("account-status", `Active session ${activeSession}: ${sessions[activeSession] || "—"}`);
    const names = ["flinstone", "root", ...extraUsers.filter(name => name !== "flinstone" && name !== "root")];
    text("account-users", `Users: ${names.join(", ")}`);
  }
  async function resolveLabDns(host) {
    if (!core.labDnsNameOk(host)) {
      await sendGuest(`dnsack ${host} fail\n`);
      return;
    }
    let ip = "fail";
    let ipv6 = "";
    try {
      const response = await fetch(core.labDnsRequestUrl(location.href, host));
      const body = await response.json();
      if (body && body.ok) {
        if (body.ip) ip = body.ip;
        if (body.ipv6) ipv6 = body.ipv6;
      }
    } catch (_) {
      /* guest prints unknown host */
    }
    await sendGuest(ipv6 ? `dnsack ${host} ${ip} ${ipv6}\n` : `dnsack ${host} ${ip}\n`);
  }
  async function applyGuestLine(line) {
    const event = core.parseGuestLine(line);
    if (!event) return;
    if (event.type === "session") {
      activeSession = event.session;
      sessions[activeSession] = event.user;
      renderSessions();
      return;
    }
    if (event.type === "switchuser") {
      sessions[activeSession] = event.user;
      renderSessions();
      return;
    }
    if (event.type === "dns") {
      if (event.host) await resolveLabDns(event.host);
      return;
    }
    if (event.type !== "server" || !relay) return;
    try {
      if (event.op === "leave" || event.op === "kill") {
        relay.leave();
        renderRelayStatus("Disconnected");
        renderRoster([]);
        appendChat("[relay] leave");
        return;
      }
      if (event.op === "host" || event.op === "join") {
        renderRelayStatus("Connecting…");
        await relay.connect(principal());
        return;
      }
      if (event.op === "msg" || event.op === "announce") {
        if (!relay.connected) await relay.connect(principal());
        if (relay.sendMessage(event.text || ""))
          appendChat(`${relay.display || principal()}: ${event.text || ""}`);
        return;
      }
      if (event.op === "connected") {
        renderRoster(relay.members || []);
        return;
      }
      appendChat(`[guest] ${event.op}${event.text ? " " + event.text : ""}`);
    } catch (error) {
      renderRelayStatus(error.message || "Relay failed");
      appendChat(`[error] ${error.message || "Relay failed"}`);
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
  let guestInput = Promise.resolve();
  function enqueueGuest(work) {
    const run = guestInput.then(work, work);
    guestInput = run.catch(() => {});
    return run;
  }
  async function sendGuest(value) {
    if (!emulator || typeof emulator.sendText !== "function") return;
    await enqueueGuest(() => emulator.sendText(value));
  }
  const controller = core.createController({
    marker: "FLINTSTONE_KERNEL_BOOT_OK", setState,
    postReady: () => window.parent.postMessage({ source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: info.shortCommit }, config.parentOrigin),
    createEmulator: async (settings) => {
      if (typeof config.createEmulator !== "function") throw new Error("No validated x86-64 browser emulator is configured");
      serial.textContent = "";
      activeSession = 1;
      sessions = { 1: "flinstone" };
      extraUsers = [];
      renderSessions();
      emulator = await config.createEmulator({ ...settings, serialByte: byte => {
        const ch = String.fromCharCode(byte);
        serial.textContent = (serial.textContent + ch).slice(-65536);
        if (ch === "\n") {
          const line = serialLine.replace(/\r$/, "");
          serialLine = "";
          void applyGuestLine(line);
        } else {
          serialLine = (serialLine + ch).slice(-4096);
        }
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
    if (info.serverRelayPort && !(window.FLINTSTONE_LAB_CONFIG && Object.prototype.hasOwnProperty.call(window.FLINTSTONE_LAB_CONFIG, "relayPort"))) {
      config.relayPort = info.serverRelayPort;
    }
    text("commit", info.shortCommit); text("architecture", info.architecture);
    text("emulator", info.browserEmulator); text("artifact", info.artifact);
    renderRuntimeMode();
    renderCaps(info.capabilities);
    renderSessions();
    setupRelay();
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
      try { await action(); } catch (error) {
        if (id === "pause" || id === "resume") {
          const node = document.getElementById("status");
          if (node) {
            node.className = "blocked";
            node.textContent = `Failed: ${error.message || String(error)}`;
          }
        } else {
          controller.fail(error);
        }
      } finally { busy = false; }
    };
  }
  screen.addEventListener("click", () => screen.focus());
  function sendKeyEventToGuest(event) {
    if (!emulator || typeof emulator.sendKey !== "function") return false;
    const codes = window.FlintstoneQemuKeys && window.FlintstoneQemuKeys.qcodesForEvent(event);
    if (!codes) return false;
    event.preventDefault();
    enqueueGuest(() => emulator.sendKey(codes)).catch(error => console.warn(error));
    return true;
  }
  screen.addEventListener("keydown", event => { sendKeyEventToGuest(event); });
  document.addEventListener("keydown", event => {
    if (event.defaultPrevented) return;
    if (core.isFormTypingTarget(event.target)) return;
    sendKeyEventToGuest(event);
  });
  document.getElementById("guest-cmd-form")?.addEventListener("submit", async event => {
    event.preventDefault();
    const input = document.getElementById("guest-cmd");
    const line = input?.value.trim();
    if (!line) return;
    await sendGuest(`${line}\n`);
    input.value = "";
    screen.focus();
  });
  const form = document.getElementById("switch-user");
  if (form) {
    form.addEventListener("submit", async event => {
      event.preventDefault();
      const name = document.getElementById("account-name").value.trim();
      const script = core.guestSwitchLines(name);
      if (!script) return;
      await sendGuest(script);
    });
    document.getElementById("account-new-session").onclick = async () => {
      const name = document.getElementById("account-name").value.trim();
      await sendGuest(core.guestNewSessionLines(name));
    };
    document.getElementById("account-register")?.addEventListener("click", async () => {
      const name = document.getElementById("account-name").value.trim();
      const secret = document.getElementById("account-secret")?.value || name;
      const script = core.guestRegisterLines(name, secret);
      if (!script) return;
      await sendGuest(script);
      if (!extraUsers.includes(name)) extraUsers.push(name);
      renderSessions();
    });
  }
  load().catch((error) => controller.fail(error));
})();
