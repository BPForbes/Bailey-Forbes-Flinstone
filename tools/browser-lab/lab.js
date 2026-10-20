(() => {
  "use strict";
  const core = window.FlintstoneLabCore;
  const labBase = document.currentScript ? new URL(".", document.currentScript.src) : new URL("./", location.href);
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
    const term = document.getElementById("wasm-term");
    if (term) term.textContent = "";
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
    for (const id of ["account-login", "account-new-session", "account-register",
                      "netlab-pod", "netlab-start", "netlab-hosts", "netlab-scan", "netlab-wpa",
                      "netlab-topology", "netlab-arpdemo", "netlab-capture", "netlab-defend",
                      "netlab-stop", "netlab-session-pod",
                      "netlab-pod-create", "netlab-pod-join", "netlab-pod-members",
                      "netlab-pod-leave", "netlab-rs-listen", "netlab-rs-accept", "netlab-rs-end",
                      "netlab-status", "netlab-consent", "netlab-rsexplain"]) {
      const node = document.getElementById(id);
      if (node) node.disabled = !ready;
    }
    renderPowerline(state);
    if (ready) {
      screen.focus();
      if (!labIsolated && (!relay || !relay.connected)) void connectRelay();
    }
  };
  let info;
  let busy = false;
  let emulator = null;
  let extraUsers = [];
  let sessions = { 1: "Flinstone" };
  // Built-in accounts, matched regardless of case like every account name.
  const isBuiltInUser = name => /^(flinstone|root)$/i.test(name);
  let activeSession = 1;
  let relay = null;
  // Files other users sent, waiting for `server file accept` (each with the principal it was sent to).
  const MAX_WAITING_FILES = 16;
  let waitingFiles = [];
  let connectRelay = async () => {};
  let serialLine = "";
  // Lab network mode: when a netlab MitM+RS session is active the guest emits
  // `LAB_NETMODE lab` and we drop the real network (the relay); `LAB_NETMODE
  // live` restores it. While isolated we also suppress auto-reconnect.
  let labIsolated = false;
  const search = new URLSearchParams(location.search);
  const validation = search.get("validate") === "1";
  const qemuMode = validation || search.get("qemu") === "1";
  let useWasm = false;
  const usesBrowserRelay = () => info && info.serverPath === "relay";
  const principal = () => sessions[activeSession] || "Flinstone";
  const serial = document.getElementById("serial");
  const canvas = document.querySelector("#screen canvas");
  const screen = document.getElementById("screen");
  // Classic VGA is 16 indexed slots; each slot is a 24-bit (sRGB) color so the
  // JS terminal can use truecolor while the guest still writes 4-bit attributes.
  const VGA_TRUECOLOR = [
    "#1c1c1e", "#0a84ff", "#30d158", "#64d2ff",
    "#ff453a", "#bf5af2", "#ff9f0a", "#d2d2d7",
    "#636366", "#409cff", "#32d74b", "#70d7ff",
    "#ff6961", "#da8fff", "#ffd60a", "#f5f5f7",
  ];
  const VGA_CELL_W = 11;
  const VGA_CELL_H = 18;
  const VGA_FONT = '16px "JetBrains Mono", "SF Mono", ui-monospace, monospace';
  let lastScreenBytes = null;
  function renderPowerline(state) {
    const user = document.getElementById("pl-user");
    const sess = document.getElementById("pl-sess");
    const st = document.getElementById("pl-state");
    const count = Object.keys(sessions).length;
    if (user) user.textContent = `\uf007 ${sessions[activeSession] || "Flinstone"}`;
    if (sess) sess.textContent = `${activeSession}/${count}`;
    if (st) st.textContent = state || document.getElementById("status")?.textContent || "…";
  }
  // The MitM + RS panel drives netlab, which only the security build compiles
  // in. Show it only when this build's manifest says so; a lab-free build would
  // otherwise offer buttons that answer "unknown command".
  function applySecurityPanel(manifest) {
    const panel = document.querySelector(".netlab-panel");
    if (!panel) return;
    panel.hidden = !(manifest && manifest.capabilities && manifest.capabilities.securityLab === true);
  }
  // A freshly booted or reset guest starts in live mode. Clear the isolation
  // flag with the guest, so the relay is not suppressed for a guest that has
  // no netlab binding.
  function clearLabIsolation() {
    if (!labIsolated) return;
    labIsolated = false;
    renderNetmode("live");
  }
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
    if (useWasm) {
      node.textContent = "Runtime: Flinstone Shell (Emscripten sandbox) — type at shell>. Switch user, register, and server chat use the same identity and JS relay as the hosted lab.";
    } else if (info.runtimeMode === "browser-hosted" || usesBrowserRelay()) {
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
  function renderNetmode(mode) {
    const node = document.getElementById("netmode-status");
    if (!node) return;
    const isolated = mode === "lab";
    node.textContent = isolated
      ? "Lab-isolated — real network dropped"
      : "Live — real network available";
    node.dataset.mode = isolated ? "lab" : "live";
  }
  // React to the guest's LAB_NETMODE token: drop the relay (real network) for
  // the duration of a netlab MitM+RS session, restore it when the demo ends.
  function applyNetmode(mode) {
    labIsolated = mode === "lab";
    renderNetmode(mode);
    if (labIsolated) {
      if (relay) {
        relay.leave();
        renderRoster([]);
        kqSyncRoster([]);
      }
      renderRelayStatus("Lab-isolated — real network dropped");
      appendChat("[netlab] lab-isolated: real network (relay) dropped for the demo");
    } else {
      appendChat("[netlab] live: real network restored");
      if (relay) void connectRelay();
    }
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
        if (!core.isFormTypingTarget(document.activeElement)) screen.focus();
      } else if (event.type === "announcement") {
        appendChat(`[announce] ${event.text}`);
      } else if (event.type === "message") {
        appendChat(event.text);
      } else if (event.type === "roster") {
        renderRoster(event.members);
        kqSyncRoster(event.members);
      } else if (event.type === "kq") {
        if (kqReady()) emulator.kqFrame(event.toHost, event.memberId, event.op, event.bytes);
      } else if (event.type === "kq-keys") {
        if (kqReady() && !emulator.kqLearn(event.principal, event.keys))
          appendKqLog("host", "refused keys", `${event.principal}: that name is held by other keys`);
      } else if (event.type === "file") {
        receiveFileFromRelay(event);
      } else if (event.type === "file-status") {
        appendChat(`[file] ${event.text}`);
      } else if (event.type === "error") {
        appendChat(`[error] ${event.text}`);
      } else if (event.type === "closed") {
        renderRelayStatus("Disconnected");
      }
    });
    let connecting = null;
    const connect = () => {
      if (labIsolated) {
        renderRelayStatus("Lab-isolated — real network dropped");
        return Promise.resolve();
      }
      if (connecting) return connecting;
      connecting = (async () => {
        try {
          if (!relay.connected) renderRelayStatus("Connecting…");
          await relay.connect(principal());
          if (labIsolated) {
            // Isolation began mid-connect: drop the relay we just opened.
            relay.leave();
            renderRelayStatus("Lab-isolated — real network dropped");
          } else if (relay.connected)
            renderRelayStatus(`Connected as ${relay.display} (#${relay.memberId})`);
        } catch (error) {
          renderRelayStatus(error.message || "Connection failed");
        } finally {
          connecting = null;
        }
      })();
      return connecting;
    };
    connectRelay = connect;
    document.getElementById("server-host")?.addEventListener("click", () => { void connect(); });
    document.getElementById("server-join")?.addEventListener("click", () => { void connect(); });
    document.getElementById("server-leave")?.addEventListener("click", () => {
      relay?.leave();
      renderRelayStatus("Disconnected");
      renderRoster([]);
      kqSyncRoster([]);
    });
    let sendingMsg = false;
    document.getElementById("server-msg-form")?.addEventListener("submit", async event => {
      event.preventDefault();
      if (sendingMsg) return;
      const input = document.getElementById("server-msg");
      const body = input?.value.trim();
      if (!body) return;
      sendingMsg = true;
      try {
        if (!relay?.connected) await connect();
        if (relay?.sendMessage(body)) input.value = "";
      } finally {
        sendingMsg = false;
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
      button.onclick = async () => {
        await sendGuestLines(`session ${id}\n`, [/SESSION \d+ user=/]);
        screen.focus();
      };
      node.appendChild(button);
    });
    text("account-status", `Active session ${activeSession}: ${sessions[activeSession] || "—"}`);
    const names = ["Flinstone", "Root", ...extraUsers.filter(name => !isBuiltInUser(name))];
    text("account-users", `Users: ${names.join(", ")}`);
    renderPowerline(document.getElementById("status")?.textContent);
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
  async function sendFileToRelay(event) {
    if (!relay) {
      appendChat("[file] not sent: no server relay in this runtime");
      return;
    }
    // Lab isolation drops the real network; a file must not leave either.
    if (labIsolated) {
      appendChat("[file] not sent: lab-isolated — real network dropped");
      return;
    }
    try {
      if (!relay.connected) await relay.connect(principal());
      if (!relay.sendFile(event.to, event.name, event.bytes))
        appendChat(`[file] not sent: ${event.name}`);
    } catch (error) {
      appendChat(`[error] ${error.message || "Relay failed"}`);
    }
  }
  /* ---- KeyQuorum co-signed delivery (the WebAssembly shell, lab_kq.c) ---------------- */
  function kqReady() {
    return Boolean(emulator && typeof emulator.kqFrame === "function");
  }
  function appendKqLog(which, outcome, detail) {
    const node = document.getElementById("kq-log");
    if (!node) return;
    node.textContent = (node.textContent + `${which}: ${outcome}${detail ? ` — ${detail}` : ""}\n`).slice(-16384);
    node.scrollTop = node.scrollHeight;
  }
  async function sendKqToRelay(frame) {
    // Lab isolation drops the real network; KeyQuorum frames stay home too.
    if (!relay || labIsolated) return;
    try {
      if (!relay.connected) await relay.connect(principal());
      if (!relay.sendKq(frame.toHost, frame.memberId, frame.op, frame.bytes))
        appendKqLog("relay", "not sent", "a frame did not fit the relay");
    } catch (error) {
      appendChat(`[error] ${error.message || "Relay failed"}`);
    }
  }
  /* The shell learns who is on the server; this member publishes its public keys to the host. */
  let kqBuiltInNoted = false;
  function kqSyncRoster(members) {
    if (!kqReady() || !relay) return;
    emulator.kqRoster(relay.connected ? relay.memberId || 0 : 0, relay.connected ? members || [] : []);
    if (!relay.connected) return;
    // Every visitor's lab seeds the built-in accounts, so their names can never be one
    // person's: they publish no keys.
    if (isBuiltInUser(principal())) {
      if (!kqBuiltInNoted)
        appendKqLog("keys", "not published", `${principal()} is built in; register your own user to send and receive KeyQuorum files`);
      kqBuiltInNoted = true;
      return;
    }
    const keys = emulator.kqPublic(principal());
    if (!keys) {
      appendKqLog("keys", "not published", `${principal()} is held by another member's keys here`);
      return;
    }
    if (!(members || []).some(m => m.memberId === relay.memberId && m.isHost)) relay.publishKeys(keys);
  }
  function renderKqRequest(id, from, file) {
    const list = document.getElementById("kq-requests");
    if (!list || list.querySelector(`[data-delivery="${id}"]`)) return;
    const item = document.createElement("li");
    item.dataset.delivery = id;
    const label = document.createElement("span");
    label.textContent = `${file} from ${from}`;
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = "Co-sign";
    button.onclick = async () => {
      button.disabled = true;
      if (await sendGuestLines(`server file cosign ${id}\n`)) button.textContent = "Co-signed";
      else button.disabled = false;
    };
    item.append(label, " ", button);
    list.appendChild(item);
  }
  function renderKqEvent(event) {
    if (!event) return;
    if (event.kind === "checkpoint") appendKqLog(event.a, event.b, event.c);
    else if (event.kind === "request") {
      renderKqRequest(event.a, event.b, event.c);
      appendKqLog("request", `${event.b} asks you to co-sign ${event.c}`, "");
    } else if (event.kind === "received") appendKqLog("saved", event.a, "in the current directory");
  }
  setInterval(() => { if (kqReady()) emulator.kqTick(); }, 5000);
  function wireServerFiles() {
    const form = document.getElementById("server-file-form");
    form?.addEventListener("submit", async event => {
      event.preventDefault();
      const path = document.getElementById("server-file-path")?.value.trim() || "";
      const user = document.getElementById("server-file-user")?.value.trim() || "";
      const cosigned = document.getElementById("server-file-kq")?.checked;
      // One shell line, built only from a plain path and a user name.
      if (!/^[A-Za-z0-9._/-]{1,63}$/.test(path) || !core.guestUserNameOk(user)) {
        appendKqLog("send", "not sent", "a file in the lab shell and a user name are needed");
        return;
      }
      if (cosigned && !kqReady()) {
        appendKqLog("send", "not sent", "KeyQuorum runs in the WebAssembly shell only");
        return;
      }
      await sendGuestLines(cosigned ? `server file send -t ${path} -user ${user}\n`
                                    : `server file -user ${user} ${path}\n`);
    });
    document.getElementById("server-file-accept")?.addEventListener("click", () => {
      void sendGuestLines("server file accept\n");
    });
    document.getElementById("server-file-decline")?.addEventListener("click", () => {
      void sendGuestLines("server file decline\n");
    });
  }
  wireServerFiles();
  function receiveFileFromRelay(event) {
    if (!core.fileRecvLines(event.from, event.name, event.bytes)) {
      appendChat(`[file] refused a file from ${event.from}: unsafe name`);
      return;
    }
    waitingFiles.push({ to: relay.principal, from: event.from, name: event.name, bytes: event.bytes });
    if (waitingFiles.length > MAX_WAITING_FILES) {
      const dropped = waitingFiles.shift();
      appendChat(`[file] too many files waiting; dropped ${dropped.name} from ${dropped.from}`);
    }
    appendChat(`[file] ${event.from} sent ${event.name} (${event.bytes.length} bytes) — run \`server file accept\` to save it`);
  }
  /* Answers `server file accept|decline` for the session's own user; others' files keep waiting. */
  async function answerWaitingFiles(accept) {
    const me = principal();
    const mine = waitingFiles.filter(f => f.to === me);
    waitingFiles = waitingFiles.filter(f => f.to !== me);
    if (!accept) {
      appendChat(`[file] declined ${mine.length} file(s)`);
      return;
    }
    if (!(await sendGuest(core.fileAcceptLines(mine)))) {
      waitingFiles = mine.concat(waitingFiles);
      appendChat("[file] the guest is not ready; the files are still waiting");
    }
  }
  async function applyGuestLine(line) {
    const event = core.parseGuestLine(line);
    if (!event) return;
    if (event.type === "session") {
      activeSession = event.session;
      sessions[activeSession] = event.user;
      rememberUser(event.user);
      renderSessions();
      return;
    }
    if (event.type === "switchuser") {
      sessions[activeSession] = event.user;
      rememberUser(event.user);
      renderSessions();
      void syncRelayPrincipal();
      return;
    }
    if (event.type === "dns") {
      if (event.host) await resolveLabDns(event.host);
      return;
    }
    if (event.type === "netmode") {
      applyNetmode(event.mode);
      return;
    }
    if (event.type === "file") {
      await sendFileToRelay(event);
      return;
    }
    if (event.type === "file-accept" || event.type === "file-decline") {
      await answerWaitingFiles(event.type === "file-accept");
      return;
    }
    if (event.type !== "server" || !relay) return;
    try {
      if (event.op === "leave" || event.op === "kill") {
        relay.leave();
        renderRelayStatus("Disconnected");
        renderRoster([]);
        kqSyncRoster([]);
        appendChat("[relay] leave");
        return;
      }
      // While lab-isolated the relay stays down: reconnecting here would bypass
      // the isolation boundary the guest enforces.
      if (labIsolated && (event.op === "host" || event.op === "join" ||
                          event.op === "msg" || event.op === "announce")) {
        renderRelayStatus("Lab-isolated — real network dropped");
        return;
      }
      if (event.op === "host" || event.op === "join") {
        renderRelayStatus("Connecting…");
        await relay.connect(principal());
        return;
      }
      if (event.op === "msg" || event.op === "announce") {
        if (!relay.connected) await relay.connect(principal());
        relay.sendMessage(event.text || "");
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
    lastScreenBytes = bytes;
    document.documentElement.dataset.vgaCell = "F";
    canvas.width = 80 * VGA_CELL_W;
    canvas.height = 25 * VGA_CELL_H;
    const ctx = canvas.getContext("2d");
    ctx.font = VGA_FONT;
    ctx.textBaseline = "top";
    for (let i = 0; i < 2000; i++) {
      const ch = bytes[i * 2], attr = bytes[i * 2 + 1];
      const x = (i % 80) * VGA_CELL_W, y = Math.floor(i / 80) * VGA_CELL_H;
      ctx.fillStyle = VGA_TRUECOLOR[(attr >> 4) & 7];
      ctx.fillRect(x, y, VGA_CELL_W, VGA_CELL_H);
      ctx.fillStyle = VGA_TRUECOLOR[attr & 15];
      if (ch >= 32 && ch <= 126) ctx.fillText(String.fromCharCode(ch), x, y + 1);
    }
    document.getElementById("display-placeholder").hidden = true;
  }
  if (document.fonts && document.fonts.ready) {
    document.fonts.ready.then(() => { if (lastScreenBytes) renderScreen(lastScreenBytes); });
  }
  const options = () => ({
    artifactUrl: new URL(`${config.artifactBaseUrl}/${info.artifact}?v=${info.shortCommit}`, location.href).href,
    memorySize: info.recommendedRamBytes,
    sha256: info.sha256,
    onScreen: renderScreen, onDiagnostic: text => console.warn(text),
    onKqFrame: frame => { void sendKqToRelay(frame); },
    onKqEvent: event => renderKqEvent(event),
  });
  let serialTail = "";
  const serialWaiters = [];
  function noteSerialChar(ch) {
    serialTail = (serialTail + ch).slice(-8192);
    for (let i = serialWaiters.length - 1; i >= 0; i--) {
      const slice = serialTail.slice(serialWaiters[i].from);
      if (serialWaiters[i].re.test(slice)) {
        const waiter = serialWaiters.splice(i, 1)[0];
        clearTimeout(waiter.timer);
        waiter.resolve(true);
      }
    }
  }
  function waitSerial(re, timeoutMs, from) {
    const start = from == null ? 0 : from;
    if (re.test(serialTail.slice(start))) return Promise.resolve(true);
    return new Promise(resolve => {
      const waiter = {
        re,
        from: start,
        resolve,
        timer: setTimeout(() => {
          const idx = serialWaiters.indexOf(waiter);
          if (idx >= 0) serialWaiters.splice(idx, 1);
          resolve(false);
        }, timeoutMs),
      };
      serialWaiters.push(waiter);
    });
  }
  function rememberUser(name) {
    if (!name || isBuiltInUser(name)) return;
    if (!extraUsers.includes(name)) extraUsers.push(name);
  }
  async function syncRelayPrincipal() {
    if (!relay || !relay.connected) return;
    if (relay.principal === principal()) return;
    try {
      if (!relay.connected) renderRelayStatus("Connecting…");
      await relay.connect(principal());
      if (relay.connected)
        renderRelayStatus(`Connected as ${relay.display} (#${relay.memberId})`);
    } catch (error) {
      renderRelayStatus(error.message || "Connection failed");
    }
  }
  let guestInput = Promise.resolve();
  function enqueueGuest(work) {
    const run = guestInput.then(work, work);
    guestInput = run.catch(() => {});
    return run;
  }
  function guestReady() {
    return emulator && typeof emulator.sendText === "function";
  }
  async function sendGuest(value) {
    if (!guestReady()) return false;
    await enqueueGuest(() => emulator.sendText(value));
    return true;
  }
  async function sendGuestLines(script, patterns) {
    if (!guestReady()) {
      text("account-status", "Guest is not ready — wait for Ready, then retry.");
      return false;
    }
    const lines = String(script || "").split("\n");
    if (lines[lines.length - 1] === "") lines.pop();
    for (let i = 0; i < lines.length; i++) {
      const from = serialTail.length;
      if (!await sendGuest(`${lines[i]}\n`)) return false;
      const pattern = (patterns && patterns[i]) || /SWITCHUSER user=|SESSION \d+ user=|Password:|unknown user|authentication failed|\bok\b|WHOAMI |@flintstone>|shell>/;
      await waitSerial(pattern, 4000, from);
    }
    return true;
  }
  const controller = core.createController({
    marker: "FLINTSTONE_KERNEL_BOOT_OK", setState,
    postReady: () => window.parent.postMessage({ source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: info.shortCommit }, config.parentOrigin),
    createEmulator: async (settings) => {
      if (typeof config.createEmulator !== "function") throw new Error("No validated x86-64 browser emulator is configured");
      serial.textContent = "";
      serialTail = "";
      activeSession = 1;
      sessions = { 1: "Flinstone" };
      extraUsers = [];
      renderSessions();
      // The guest streams raw bytes one at a time, including multi-byte
      // UTF-8 sequences (e.g. an em dash is 3 bytes). String.fromCharCode
      // on each raw byte mangles those into mojibake; a streaming
      // TextDecoder buffers an incomplete sequence across calls and only
      // yields a character once it is complete, with no added delay for
      // the ASCII bytes that make up most guest output.
      const serialDecoder = new TextDecoder("utf-8", { fatal: false });
      emulator = await config.createEmulator({ ...settings, serialByte: byte => {
        const ch = serialDecoder.decode(new Uint8Array([byte]), { stream: true });
        if (!ch) { settings.serialByte(byte); return; }
        serial.textContent = (serial.textContent + ch).slice(-65536);
        noteSerialChar(ch);
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
  function wasmFallbackInfo() {
    return core.validateManifest({
      schemaVersion: 2,
      commit: "0".repeat(40),
      shortCommit: "wasm-lab",
      architecture: "wasm32",
      browserEmulator: "Emscripten",
      artifact: "flintstone.wasm",
      sha256: "0".repeat(64),
      bootSuccessMarker: "FLINTSTONE_KERNEL_BOOT_OK",
      validationOutcome: "wasm-shell",
      bootableCandidate: true,
      bootable: true,
      v86Compatible: false,
      browserCompatible: false,
      bootSuccessMarkerImplemented: true,
      recommendedRamBytes: 16,
      blockers: [],
      capabilities: {
        identity: true, hostedLabSessions: true, keyboard: true,
        filesystem: true, network: true, server: true,
      },
      serverPath: "relay",
    });
  }
  async function wasmModulePresent() {
    try {
      const response = await fetch(new URL("wasm/flintstone.js", labBase), { cache: "no-store" });
      return response.ok;
    } catch (_) {
      return false;
    }
  }
  function applyRuntimeChrome() {
    document.body.classList.toggle("lab-wasm", useWasm);
    document.body.classList.toggle("lab-qemu", !useWasm);
    const displayLabel = document.getElementById("display-label");
    const bezelRuntime = document.getElementById("bezel-runtime");
    if (displayLabel) {
      displayLabel.textContent = useWasm
        ? "Flinstone Shell · click here to type at shell>"
        : "VGA text 80×25 · click here to type";
    }
    if (bezelRuntime) {
      bezelRuntime.textContent = useWasm ? "Flinstone Shell" : "SeaBIOS · QEMU Wasm";
    }
  }
  async function load() {
    setState(core.STATES.LOADING);
    useWasm = !qemuMode && await wasmModulePresent() && typeof window.createFlintstoneWasm === "function";
    if (useWasm) config.createEmulator = window.createFlintstoneWasm;
    applyRuntimeChrome();
    try {
      const response = await fetch(config.metadataUrl, { cache: "no-store" });
      if (!response.ok) throw new Error(`Metadata request failed: HTTP ${response.status}`);
      info = core.validateManifest(await response.json());
    } catch (error) {
      if (!useWasm) throw error;
      info = wasmFallbackInfo();
    }
    if (info.serverRelayPort && !(window.FLINTSTONE_LAB_CONFIG && Object.prototype.hasOwnProperty.call(window.FLINTSTONE_LAB_CONFIG, "relayPort"))) {
      config.relayPort = info.serverRelayPort;
    }
    text("commit", info.shortCommit); text("architecture", useWasm ? "wasm32" : info.architecture);
    text("emulator", useWasm ? "Emscripten" : info.browserEmulator); text("artifact", useWasm ? "flintstone.wasm" : info.artifact);
    renderRuntimeMode();
    renderCaps(info.capabilities);
    applySecurityPanel(info);
    renderSessions();
    setupRelay();
    if (!canBoot()) {
      setState(core.STATES.BLOCKED, info.blockers.join("; "));
      return;
    }
    await controller.boot(options());
  }
  function canBoot() {
    if (useWasm) return typeof config.createEmulator === "function";
    return info && info.bootable && (info.browserCompatible || (validation && info.bootableCandidate));
  }
  for (const [id, action] of Object.entries({ boot: () => controller.boot(options()), pause: () => controller.pause(), resume: () => controller.resume(), reset: () => controller.reset(options()), poweroff: () => controller.powerOff() })) {
    document.getElementById(id).onclick = async () => {
      if (busy || ((id === "boot" || id === "reset") && !canBoot())) return;
      busy = true;
      if (id === "boot" || id === "reset" || id === "poweroff") clearLabIsolation();
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
    if (emulator && typeof emulator.typeChar === "function") {
      let ch = "";
      if (event.key === "Enter") ch = "\n";
      else if (event.key === "Backspace") ch = "\b";
      else if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) ch = event.key;
      if (!ch) return false;
      event.preventDefault();
      enqueueGuest(() => emulator.typeChar(ch)).catch(error => console.warn(error));
      return true;
    }
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
    if (!await sendGuestLines(line)) return;
    input.value = "";
    screen.focus();
  });

  // MitM + RS lab panel: each button sends a `netlab ...` line to the shell,
  // so the network system is driven without the CLI. The lab/live network
  // switch rides the LAB_NETMODE token the guest emits (see applyNetmode).
  function netlabPod() {
    const sel = document.getElementById("netlab-pod");
    return (sel && sel.value) || "redteam-lab";
  }
  // Reads a text field, trims it, and keeps only its first whitespace-
  // delimited token (names/pods/victims are single shell words), falling
  // back to a sane default so a blank field never sends a bare command.
  function netlabToken(id, fallback) {
    const el = document.getElementById(id);
    const first = (el && el.value || "").trim().split(/\s+/)[0];
    return first || fallback;
  }
  function wireNetlab(id, build) {
    document.getElementById(id)?.addEventListener("click", () => {
      void sendGuestLines(`${build(netlabPod())}\n`);
      screen.focus();
    });
  }
  // Like wireNetlab, but the command doesn't depend on the segment selector.
  function wireNetlabPlain(id, build) {
    document.getElementById(id)?.addEventListener("click", () => {
      void sendGuestLines(`${build()}\n`);
      screen.focus();
    });
  }

  // Network segment: recon + MitM on the fabricated hosts.
  wireNetlab("netlab-hosts", pod => `netlab hosts ${pod}`);
  wireNetlab("netlab-scan", pod => `netlab scan ${pod}`);
  wireNetlab("netlab-wpa", pod => `netlab wpa ${pod}`);
  wireNetlab("netlab-topology", pod => `netlab topology ${pod}`);
  wireNetlab("netlab-arpdemo", pod => `netlab arp-demo ${pod}`);
  wireNetlab("netlab-capture", pod => `netlab capture ${pod}`);

  // Pod (private group): membership and host succession.
  wireNetlabPlain("netlab-pod-create", () =>
    `netlab pod create ${netlabToken("netlab-session-pod", "redcell")}`);
  wireNetlabPlain("netlab-pod-join", () => "netlab pod join");
  wireNetlabPlain("netlab-pod-members", () => "netlab pod members");
  wireNetlabPlain("netlab-pod-leave", () => "netlab pod leave");

  // Reverse shell (simulated): open/close against a named mock victim.
  wireNetlabPlain("netlab-rs-listen", () => "netlab listen");
  wireNetlabPlain("netlab-rs-accept", () => "netlab accept");
  wireNetlabPlain("netlab-rs-end", () => "netlab end");
  wireNetlabPlain("netlab-status", () => "netlab status");

  // Network isolation and reference text.
  wireNetlabPlain("netlab-start", () => "netlab start");
  wireNetlabPlain("netlab-stop", () => "netlab stop");
  wireNetlabPlain("netlab-consent", () => "netlab consent");
  wireNetlabPlain("netlab-rsexplain", () => "netlab rs-explain");
  wireNetlabPlain("netlab-defend", () => "netlab defend");

  const form = document.getElementById("switch-user");
  if (form) {
    form.addEventListener("submit", async event => {
      event.preventDefault();
      const name = document.getElementById("account-name").value.trim();
      const script = core.guestSwitchLines(name);
      if (!script) return;
      await sendGuestLines(script, [/SWITCHUSER user=|unknown user/]);
      screen.focus();
    });
    document.getElementById("account-new-session").onclick = async () => {
      const name = document.getElementById("account-name").value.trim();
      await sendGuestLines(core.guestNewSessionLines(name), [
        /SESSION \d+ user=/,
        /SWITCHUSER user=|unknown user/,
      ]);
      screen.focus();
    };
    document.getElementById("account-register")?.addEventListener("click", async () => {
      const name = document.getElementById("account-name").value.trim();
      const secret = document.getElementById("account-secret")?.value || name;
      const script = core.guestRegisterLines(name, secret);
      if (!script) return;
      const from = serialTail.length;
      const sent = await sendGuestLines(script, [
        /SWITCHUSER user=Root|unknown user/,
        /Password:|unknown command|need elevation|usage:/,
        /\bok\b|authentication failed/,
      ]);
      const chunk = serialTail.slice(from);
      if (sent && /\bok\b/.test(chunk) && !/authentication failed/.test(chunk))
        rememberUser(name);
      renderSessions();
      screen.focus();
    });
  }
  load().catch((error) => controller.fail(error));
})();
