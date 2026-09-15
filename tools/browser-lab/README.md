# Flintstone browser lab

The default lab page sandboxes the freestanding identity shell as
**Flinstone Shell** (Emscripten WebAssembly). Type directly at `shell>` in that
terminal. The kernel serial panel is omitted in this mode because it duplicated
the same transcript. Switch user, register, sessions, and `server` verbs use
the same guest serial protocol and JS relay as before.

`make wasm` builds a host smoke binary (`dist/flintstone_wasm_host`) and, when
`emcc` is on `PATH`, `tools/browser-lab/wasm/flintstone.js` plus
`flintstone.wasm`. GitHub Actions installs a pinned Emscripten SDK and uploads
those modules; they are not committed.

QEMU WebAssembly still boots the hashed x86-64 BIOS disk for independent
hardware validation:

| URL | Runtime |
|-----|---------|
| `/tools/browser-lab/` (default) | Flinstone Shell (Emscripten) when `flintstone.js` is present; otherwise QEMU if the manifest is browser-compatible |
| `?qemu=1` | Force the pinned QEMU Wasm PC |
| `?validate=1` | CI / local candidate validation (QEMU + disk SHA-256) |

The QEMU path verifies the disk SHA-256 before creating the worker and declares
the guest ready only after COM1 supplies the complete
`FLINTSTONE_KERNEL_BOOT_OK` line. Display validation requires the diagnostic
VGA cell `F` with attribute `0x07`.

## Runtime modes (server chat)

| Mode | Where | Server path |
|------|-------|-------------|
| **browser-hosted** | Website lab (Flinstone Shell by default; QEMU Wasm on `?qemu=1`) | JS relay (`server-relay-hub.mjs`) speaking the same P3 session wire as `net_server.c` |
| **native local** | Hosted shell, VM, bare metal | Original C/ASM in `kernel/core/net/` — no relay |

Wire constants in `session-wire-const.js` are generated from `contracts/networking/contract_p3_session_wire.h` via `scripts/gen_session_wire_js.py`.

Start the lab with relay: `python3 scripts/serve_browser_lab.py --port 8768 --relay-port 8767`

After that marker the guest is an interactive lab shell, not the hosted ELF:

- keyboard (click the terminal / VGA bezel, then type at `shell>` in WASM mode)
- `switchuser` / `login` / `su` / `sudo` / `logout` / `whoami` / `history` / `useradd` / `session`
- lab ramfs: `dir` / `ls` / `cat` / `write` / `mkdir` / `rm` / `pwd` / `cd` and the other hosted file verbs
- lab cluster disk (`createdisk` / `writecluster` / `diskput` / …) and lab net (`ping` / `ifconfig` / `wifi` / …)
- `server host|join|leave|msg|…` through the browser relay. Local lab uses the
  WebSocket hub; GitHub Pages skips the missing `:8767` socket and uses a
  same-origin BroadcastChannel room. Sending a chat line emits it once (the
  sender's local echo is the same event path as peer delivery).
- Switch user / Register user / Guest commands send one shell line at a time
  and wait for serial (`SWITCHUSER`, `Password:`, `ok`) so `useradd` can prompt.
  A connected relay seat reconnects as the new principal after `switchuser`.
- per-user command history and VGA scrollback when switching users on the website
- up to four concurrent sessions so one operator can keep multiple registered
  accounts active (for example host as `flinstone` and admin as `root`)
- lab seeds `flinstone`/`flinstone` and `root`/`root`

Hosted FAT32, P3 sockets, and `kernel/core/net` `server host/join` are **not** in this image; the guest still runs those command names as lab analogs.

The runtime files are intentionally generated assets. Fetch the exact pinned
release and verify its digests before local use:

```sh
make wasm
make test-wasm-shell
make browser-lab-runtime
make test-browser-boot
```

`test-browser-boot` first performs the native QEMU smoke test, then drives a
real Chromium browser through the browser VM. It verifies the exact serial
marker, VGA first cell, pause/resume, reset, power-off cleanup, clean reboot,
blocked metadata, a corrupt disk, and switch-user sessions. The test records
browser evidence in `dist/browser-validation.json` and sets
`browserCompatible: true` only after all checks pass.

For interactive local use, serve with the required cross-origin isolation
headers:

```sh
make browser-kernel
./scripts/test_browser_kernel_artifact.sh
python3 ./scripts/serve_browser_lab.py
```

Open `http://127.0.0.1:8766/tools/browser-lab/` for Flinstone Shell (after
`make wasm` with `emcc`). Open `http://127.0.0.1:8766/tools/browser-lab/?validate=1`
while validating a
fresh candidate. The ordinary page refuses to boot until a manifest has a
recorded browser validation. `?validate=1` is available only to the local and
CI validation path; it still requires a native-QEMU-validated disk hash and
records browser compatibility only after the real browser test succeeds.

The page chrome is a **macOS Liquid Glass** analog: frosted window, traffic
lights, blurred side panels, JetBrains Mono + Nerd Powerline glyphs (self-hosted
under `fonts/` so COEP `require-corp` still loads them), and a 24-bit sRGB
palette for the 80×25 VGA canvas. The default terminal is **Flinstone Shell**
(`shell>`). QEMU mode still paints classic VGA text and shows kernel serial.

Use `make browser-lab-release` to build `dist/browser-lab/`: a self-contained
static package containing the verified runtime, lab UI, disk, manifest, and
browser-validation evidence. Portfolio embedding is documented in
`docs/portfolio-iframe-integration.md`.
