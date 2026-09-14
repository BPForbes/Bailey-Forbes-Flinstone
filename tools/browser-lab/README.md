# Flintstone browser lab

The lab runs the freestanding x86_64 BIOS disk in a pinned QEMU WebAssembly
runtime. It verifies the disk SHA-256 before creating the worker and declares
the guest ready only after COM1 supplies the complete
`FLINTSTONE_KERNEL_BOOT_OK` line.

The runtime files are intentionally generated assets. Fetch the exact pinned
release and verify its digests before local use:

```sh
make browser-lab-runtime
make test-browser-boot
```

`test-browser-boot` first performs the native QEMU smoke test, then drives a
real Chromium browser through the browser VM. It verifies the exact serial
marker, VGA text memory, pause/resume, reset, power-off cleanup, clean reboot,
blocked metadata, and a corrupt disk. The test records browser evidence in
`dist/browser-validation.json` and sets `browserCompatible: true` only after
all checks pass.

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
browser-validation evidence. The portfolio can embed that published directory
after it is deployed.
