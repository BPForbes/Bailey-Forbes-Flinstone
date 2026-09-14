# Flintstone browser lab

The lab boots the freestanding x86_64 BIOS disk in a pinned QEMU WebAssembly
runtime. It verifies the disk SHA-256 before creating the worker and declares
the guest ready only after COM1 supplies the complete
`FLINTSTONE_KERNEL_BOOT_OK` line. Display validation requires the diagnostic
VGA cell `F` with attribute `0x07`.

After that marker the guest is an interactive lab shell, not the hosted ELF:

- keyboard (click the VGA bezel, then type)
- `login` / `su` / `logout` / `whoami` / `useradd` / `session`
- up to four concurrent sessions so one operator can keep multiple registered
  accounts active (for example host as `flinstone` and admin as `root`)
- lab seeds `flinstone`/`flinstone` and `root`/`root`

Filesystem, networking, and `server host/join` are **not** in this image.

The runtime files are intentionally generated assets. Fetch the exact pinned
release and verify its digests before local use:

```sh
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

Open `http://127.0.0.1:8766/tools/browser-lab/?validate=1` while validating a
fresh candidate. The ordinary page refuses to boot until a manifest has a
recorded browser validation. `?validate=1` is available only to the local and
CI validation path; it still requires a native-QEMU-validated disk hash and
records browser compatibility only after the real browser test succeeds.

Use `make browser-lab-release` to build `dist/browser-lab/`: a self-contained
static package containing the verified runtime, lab UI, disk, manifest, and
browser-validation evidence. Portfolio embedding is documented in
`docs/portfolio-iframe-integration.md`.
