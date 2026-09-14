(() => {
  "use strict";
  const core = window.FlintstoneLabCore;
  const config = Object.assign({
    metadataUrl: "../../dist/build-info.json", artifactBaseUrl: "../../dist",
    parentOrigin: "https://bailey-forbes.com",
  }, window.FLINTSTONE_LAB_CONFIG || {});
  const text = (id, value) => { document.getElementById(id).textContent = value; };
  const setState = (state, detail) => {
    const node = document.getElementById("status");
    node.className = [core.STATES.BLOCKED, core.STATES.FAILED].includes(state) ? "blocked" : "";
    node.textContent = detail ? `${state}: ${detail}` : state;
  };
  let info;
  const options = () => ({
    artifactUrl: new URL(`${config.artifactBaseUrl}/${info.artifact}?v=${info.shortCommit}`, location.href).href,
    memorySize: info.recommendedRamBytes,
  });
  const controller = core.createController({
    marker: "FLINTSTONE_KERNEL_BOOT_OK", setState,
    postReady: () => window.parent.postMessage({ source: "flinstone-guest", type: "ready", schemaVersion: 1, commit: info.shortCommit }, config.parentOrigin),
    createEmulator: async (settings) => {
      if (typeof config.createEmulator !== "function") throw new Error("No validated x86-64 browser emulator is configured");
      return config.createEmulator(settings);
    },
  });
  async function load() {
    setState(core.STATES.LOADING);
    const response = await fetch(config.metadataUrl, { cache: "no-store" });
    if (!response.ok) throw new Error(`Metadata request failed: HTTP ${response.status}`);
    info = core.validateManifest(await response.json());
    text("commit", info.shortCommit); text("architecture", info.architecture);
    text("emulator", info.browserEmulator); text("artifact", info.artifact);
    if (!info.bootable || !(info.browserCompatible ?? info.v86Compatible)) {
      setState(core.STATES.BLOCKED, info.blockers.join("; "));
      return;
    }
    await controller.boot(options());
  }
  document.getElementById("boot").onclick = async () => { try { await controller.boot(options()); } catch (_) {} };
  document.getElementById("pause").onclick = () => controller.pause();
  document.getElementById("resume").onclick = () => controller.resume();
  document.getElementById("reset").onclick = async () => { try { await controller.reset(options()); } catch (_) {} };
  document.getElementById("poweroff").onclick = () => controller.powerOff();
  load().catch((error) => controller.fail(error));
})();
