((root, factory) => {
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  else root.FlintstoneLabCore = api;
})(typeof globalThis === "object" ? globalThis : this, () => {
  "use strict";

  const STATES = Object.freeze({
    LOADING: "Loading", BOOTING: "Booting", READY: "Ready",
    PAUSED: "Paused", FAILED: "Failed", BLOCKED: "Blocked", OFF: "Powered off",
  });

  const SHA256 = /^[0-9a-f]{64}$/;
  const COMMIT = /^[0-9a-f]{40}$/;
  const nonemptyString = value => typeof value === "string" && value.length > 0;
  const exactSerialLine = (serial, marker) =>
    nonemptyString(serial) && serial.split(/\r?\n/).includes(marker);
  const validDiagnosticVga = bytes =>
    Boolean(bytes && bytes.length === 4000 && bytes[0] === 0x46 && bytes[1] === 0x07);

  function validRuntimeDigests(files) {
    return files && typeof files === "object" && !Array.isArray(files) &&
      Object.keys(files).length > 0 && Object.entries(files).every(([name, digest]) =>
        nonemptyString(name) && !name.includes("/") && !name.includes("\\") &&
        ![".", ".."].includes(name) && SHA256.test(digest));
  }

  function validBrowserEvidence(info) {
    const evidence = info.browserValidation;
    return evidence && typeof evidence === "object" && !Array.isArray(evidence) &&
      evidence.commit === info.commit &&
      evidence.artifactSha256 === info.sha256 &&
      nonemptyString(evidence.runtime) &&
      COMMIT.test(evidence.runtimeCommit) &&
      validRuntimeDigests(evidence.runtimeFiles) &&
      nonemptyString(evidence.testedAt) && !Number.isNaN(Date.parse(evidence.testedAt)) &&
      nonemptyString(evidence.browser) &&
      exactSerialLine(evidence.serial, info.bootSuccessMarker) &&
      Array.isArray(evidence.checks) && evidence.checks.includes("exact-marker") &&
      evidence.checks.includes("vga-text-memory");
  }

  function validateManifest(info) {
    if (!info || ![1, 2].includes(info.schemaVersion)) throw new Error("Unsupported browser artifact manifest schema");
    for (const field of ["commit", "shortCommit", "architecture", "browserEmulator", "artifact", "sha256", "bootSuccessMarker", "validationOutcome"]) {
      if (typeof info[field] !== "string" || !info[field]) throw new Error(`Manifest field ${field} is invalid`);
    }
    if (!SHA256.test(info.sha256) || info.artifact.includes("/") || [".", ".."].includes(info.artifact)) {
      throw new Error("Manifest artifact identity is invalid");
    }
    for (const field of ["bootable", "v86Compatible", "bootSuccessMarkerImplemented"]) {
      if (typeof info[field] !== "boolean") throw new Error(`Manifest field ${field} is invalid`);
    }
    if (info.schemaVersion === 2 && (typeof info.browserCompatible !== "boolean" ||
      typeof info.bootableCandidate !== "boolean" ||
      typeof info.capabilities !== "object" || !info.capabilities || Array.isArray(info.capabilities))) {
      throw new Error("Schema 2 browser compatibility fields are invalid");
    }
    if (info.schemaVersion === 2 && info.browserCompatible && !validBrowserEvidence(info)) {
      throw new Error("Browser-compatible manifest lacks independent validation evidence");
    }
    if (!Number.isSafeInteger(info.recommendedRamBytes) || info.recommendedRamBytes <= 0 || !Array.isArray(info.blockers)) {
      throw new Error("Manifest runtime requirements are invalid");
    }
    return info;
  }

  function isTrustedReadyEvent(event, { allowedOrigin, guestWindow, commit }) {
    const data = event && event.data;
    return Boolean(event && event.origin === allowedOrigin && event.source === guestWindow &&
      data && data.source === "flinstone-guest" && data.type === "ready" &&
      data.schemaVersion === 1 && data.commit === commit);
  }

  function createController({ createEmulator, setState, postReady, marker }) {
    let emulator = null;
    let line = "";
    let readySent = false;
    let paused = false;
    let generation = 0;
    let timer;
    const state = (value, detail = "") => setState(value, detail);
    const serialByte = (value) => {
      const char = typeof value === "number" ? String.fromCharCode(value) : value;
      if (char === "\n") {
        if (line.replace(/\r$/, "") === marker && !readySent) {
          readySent = true;
          clearTimeout(timer);
          state(STATES.READY);
          postReady();
        }
        line = "";
      } else if (typeof char === "string") {
        line = (line + char).slice(-4096);
      }
    };
    return {
      async boot(options) {
        if (emulator) await this.powerOff();
        clearTimeout(timer); const current = ++generation;
        readySent = false; paused = false; line = ""; state(STATES.BOOTING);
        timer = setTimeout(() => { if (current === generation && !readySent) this.fail(new Error("Kernel boot marker was not observed within 90 seconds")); }, 90000);
        try {
          const instance = await createEmulator({ ...options,
            serialByte: value => { if (current === generation) serialByte(value); },
            onError: error => { if (current === generation) this.fail(error); },
          });
          if (current !== generation) { await instance.destroy(); return; }
          emulator = instance;
        }
        catch (error) { if (current === generation) this.fail(error); throw error; }
      },
      async pause() {
        if (!emulator || readySent === false || paused) return false;
        await emulator.stop();
        paused = true;
        state(STATES.PAUSED);
        return true;
      },
      async resume() {
        if (!emulator || !paused) return false;
        await emulator.run();
        paused = false;
        state(readySent ? STATES.READY : STATES.BOOTING);
        return true;
      },
      async reset(options) { await this.powerOff(); await this.boot(options); },
      async powerOff() { generation++; clearTimeout(timer); if (emulator) { if (emulator.destroy) await emulator.destroy(); else await emulator.stop(); } emulator = null; readySent = false; paused = false; line = ""; state(STATES.OFF); },
      fail(error) { generation++; clearTimeout(timer); if (emulator) emulator.destroy(); emulator = null; readySent = false; paused = false; state(STATES.FAILED, error.message || String(error)); },
      serialByte,
    };
  }
  function parseGuestLine(line) {
    const text = String(line || "").replace(/\r$/, "");
    let match = /^SESSION (\d+) user=(\S+)$/.exec(text);
    if (match) return { type: "session", session: Number(match[1]), user: match[2] };
    match = /^SWITCHUSER user=(\S+)$/.exec(text);
    if (match) return { type: "switchuser", user: match[1] };
    if (text === "SERVER_FILE_ACCEPT") return { type: "file-accept" };
    if (text === "SERVER_FILE_DECLINE") return { type: "file-decline" };
    match = /^SERVER_FILE (\S+) (\S+) ((?:[0-9a-f]{2})*)$/.exec(text);
    if (match) return { type: "file", to: match[1], name: match[2], bytes: hexToBytes(match[3]) };
    match = /^SERVER_RELAY (\S+)(?: (.*))?$/.exec(text);
    if (match) {
      if (match[1] === "dns")
        return { type: "dns", host: (match[2] || "").trim() };
      const event = { type: "server", op: match[1] };
      if (match[2]) event.text = match[2];
      return event;
    }
    match = /^LAB_NETMODE (lab|live)$/.exec(text);
    if (match) return { type: "netmode", mode: match[1] };
    return null;
  }
  function hexToBytes(hex) {
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    return out;
  }
  function bytesToHex(bytes) {
    let hex = "";
    for (const b of bytes) hex += b.toString(16).padStart(2, "0");
    return hex;
  }
  /* The guest's line buffer is 128 characters, so a received file is typed in short parts. */
  const FILE_RECV_HEX_PER_LINE = 96;
  function fileRecvLines(from, name, bytes) {
    if (!guestUserNameOk(from) || !/^[A-Za-z0-9._-]{1,19}$/.test(name) || name === "." || name === "..")
      return "";
    const hex = bytesToHex(bytes || new Uint8Array(0));
    let lines = `filerecv begin ${from} ${name}\n`;
    for (let i = 0; i < hex.length; i += FILE_RECV_HEX_PER_LINE)
      lines += `filerecv data ${hex.slice(i, i + FILE_RECV_HEX_PER_LINE)}\n`;
    return lines + "filerecv end\n";
  }
  /* The answer to `server file accept`: every waiting file in order, or a line saying there are none. */
  function fileAcceptLines(files) {
    const lines = (files || []).map(f => fileRecvLines(f.from, f.name, f.bytes)).join("");
    return lines || "filerecv none\n";
  }
  function labDnsNameOk(name) {
    return typeof name === "string" && name.length > 0 && name.length <= 253 &&
      /^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$|^[A-Za-z0-9]$/.test(name);
  }
  function labDnsRequestUrl(pageHref, name) {
    const page = new URL(pageHref);
    let dir = page.pathname;
    if (!dir.endsWith("/"))
      dir = dir.slice(0, dir.lastIndexOf("/") + 1);
    const url = new URL(`${dir}lab-dns`, page.origin);
    url.searchParams.set("name", name);
    return url;
  }
  function isFormTypingTarget(node) {
    if (!node || node.nodeType !== 1) return false;
    const tag = node.tagName;
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || tag === "BUTTON") return true;
    return Boolean(node.isContentEditable);
  }
  function guestUserNameOk(name) {
    return typeof name === "string" && /^[A-Za-z][A-Za-z0-9_-]{0,14}$/.test(name);
  }
  function guestRegisterLines(name, password) {
    if (!guestUserNameOk(name)) return "";
    const secret = String(password || name).trim() || name;
    return `switchuser Root\nuseradd ${name}\n${secret}\n`;
  }
  function guestSwitchLines(name) {
    if (!guestUserNameOk(name)) return "";
    return `switchuser ${name}\n`;
  }
  function guestNewSessionLines(name) {
    const user = guestUserNameOk(name) ? name : "Flinstone";
    return `session new\nswitchuser ${user}\n`;
  }

  return { STATES, validateManifest, isTrustedReadyEvent, createController, validDiagnosticVga, exactSerialLine, parseGuestLine, labDnsNameOk, labDnsRequestUrl, isFormTypingTarget, guestUserNameOk, guestRegisterLines, guestSwitchLines, guestNewSessionLines, hexToBytes, bytesToHex, fileRecvLines, fileAcceptLines };
});
