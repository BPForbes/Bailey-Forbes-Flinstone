# Browser artifact probe

This is a static proof-of-consumption harness, not the portfolio UI and not a
copy of v86. Serve the repository root after generating metadata:

```sh
make browser-kernel
python3 -m http.server 8000
```

Open `http://localhost:8000/tools/browser-lab/`. The current metadata reports
the x86-64/boot-protocol blocker, so the page must show **Blocked** and must not
download or instantiate v86.

The harness reads defaults from the repository's `dist/`. A separately hosted
lab can configure relative or cross-origin static assets before `lab.js`:

```html
<script>
window.FLINTSTONE_LAB_CONFIG = {
  metadataUrl: "./artifacts/build-info.json",
  artifactBaseUrl: "./artifacts",
  v86ScriptUrl: "./v86/libv86.js",
  v86WasmUrl: "./v86/v86.wasm",
  biosUrl: "./v86/seabios.bin",
  vgaBiosUrl: "./v86/vgabios.bin"
};
</script>
```

No emulator source, BIOS, or generated kernel artifact is committed here.
Those are lab infrastructure and CI output. If a future bootable i386 image
makes `bootable` and `v86Compatible` true, the harness uses the current v86
`V86` constructor with `hda`, appends `?v=<shortCommit>` for cache busting, and
uses metadata's recommended RAM.

Keep keyboard handling in the emulator. Focus the `#screen` element so browser
events flow through v86's PS/2 emulation to the Flintstone keyboard driver.
Boot, pause, resume, reset, power-off/recreate, persistence, and snapshots are
also emulator/lab responsibilities.
