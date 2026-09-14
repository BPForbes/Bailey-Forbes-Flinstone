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

  function validateManifest(info) {
    if (!info || ![1, 2].includes(info.schemaVersion)) throw new Error("Unsupported browser artifact manifest schema");
    for (const field of ["shortCommit", "architecture", "browserEmulator", "artifact", "sha256", "bootSuccessMarker", "validationOutcome"]) {
      if (typeof info[field] !== "string" || !info[field]) throw new Error(`Manifest field ${field} is invalid`);
    }
    if (!/^[0-9a-f]{64}$/.test(info.sha256) || info.artifact.includes("/") || [".", ".."].includes(info.artifact)) {
      throw new Error("Manifest artifact identity is invalid");
    }
    for (const field of ["bootable", "v86Compatible", "bootSuccessMarkerImplemented"]) {
      if (typeof info[field] !== "boolean") throw new Error(`Manifest field ${field} is invalid`);
    }
    if (info.schemaVersion === 2 && (typeof info.browserCompatible !== "boolean" || typeof info.capabilities !== "object" || !info.capabilities)) {
      throw new Error("Schema 2 browser compatibility fields are invalid");
    }
    if (info.browserCompatible && (!info.browserValidation || typeof info.browserValidation !== "object" ||
      info.browserValidation.artifactSha256 !== info.sha256 ||
      !Array.isArray(info.browserValidation.checks) || !info.browserValidation.checks.includes("exact-marker"))) {
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
        readySent = false; line = ""; state(STATES.BOOTING);
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
      async pause() { if (!emulator || readySent === false) return false; await emulator.stop(); state(STATES.PAUSED); return true; },
      async resume() { if (!emulator) return false; await emulator.run(); state(readySent ? STATES.READY : STATES.BOOTING); return true; },
      async reset(options) { await this.powerOff(); await this.boot(options); },
      async powerOff() { generation++; clearTimeout(timer); if (emulator) { if (emulator.destroy) await emulator.destroy(); else await emulator.stop(); } emulator = null; readySent = false; line = ""; state(STATES.OFF); },
      fail(error) { generation++; clearTimeout(timer); if (emulator) emulator.destroy(); emulator = null; state(STATES.FAILED, error.message || String(error)); },
      serialByte,
    };
  }
  return { STATES, validateManifest, isTrustedReadyEvent, createController };
});
