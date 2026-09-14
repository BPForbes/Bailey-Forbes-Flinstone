# Browser artifact probe

This is a static proof-of-consumption harness, not the portfolio UI and not a
copy of v86. Serve the repository root after generating metadata:

```sh
make browser-kernel
python3 -m http.server 8000
```

Open `http://localhost:8000/tools/browser-lab/`. The current metadata reports
the browser-runtime validation blocker, so the page must show **Blocked** and
must not instantiate an emulator.

The harness reads defaults from the repository's `dist/`. A separately hosted
lab can configure relative or cross-origin static assets before `lab.js`:

```html
<script>
window.FLINTSTONE_LAB_CONFIG = {
  metadataUrl: "./artifacts/build-info.json",
  artifactBaseUrl: "./artifacts",
  parentOrigin: "https://bailey-forbes.com",
  createEmulator: async ({ artifactUrl, memorySize, serialByte }) => {
    // Return a validated x86-64 PC emulator adapter with stop(), run(), and
    // destroy(). Feed each captured COM1 byte to serialByte.
  }
};
</script>
```

No emulator source, BIOS, or generated kernel artifact is committed here.
Those are lab infrastructure and CI output. The harness requires both
`bootable` and schema-v2 `browserCompatible`, appends `?v=<shortCommit>` for
cache busting, and uses metadata's recommended RAM. It posts readiness only
after the adapter supplies the exact serial marker as a complete line.

Keep keyboard handling in the emulator. Focus the `#screen` element so browser
events flow through the emulator's PS/2 path to the Flintstone keyboard driver.
Boot, pause, resume, reset, and power-off/recreate are exposed by the lab.
Persistence remains unavailable until an emulator and versioned disk format are
selected and tested.
