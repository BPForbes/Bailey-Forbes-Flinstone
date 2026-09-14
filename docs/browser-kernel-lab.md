# Flintstone browser-kernel contract

This document covers only `BPForbes/Bailey-Forbes-Flinstone`. It defines the
kernel/artifact side of a future static browser lab; the emulator UI and the
portfolio iframe remain separate projects.

## Compatibility decision

### Current freestanding boundary

`make browser-kernel` now builds `dist/flintstone.img`: a raw BIOS disk whose
MBR loads a freestanding payload, constructs identity-mapped long-mode page
tables, installs a 64-bit GDT, and enters `kernel_entry`. The entry owns its
stack, clears `.bss`, installs an early IDT, initializes COM1, and only then
emits the boot marker. The image is intentionally a **boot-boundary increment**,
not yet the hosted shell port: its serial capability line reports identity,
filesystem, networking, server, and hosted sessions as unavailable.

The build emits schema 2 metadata with `bootableCandidate: true` but
`bootable: false`. Only `scripts/test_browser_kernel_artifact.sh`, after an
independent QEMU process observes the exact serial marker, may change that copy
of the manifest to `bootable: true`. The pinned QEMU WebAssembly runtime then
boots the same hashed IDE disk in a real browser. Its browser regression test
must observe the same complete marker before it records `browserCompatible:
true`. `v86Compatible` remains false because v86 does not implement x86-64
long mode.

Build/support checks:

```sh
make test-freestanding-entry
make browser-kernel
./scripts/test_browser_kernel_artifact.sh # requires qemu-system-x86_64
make test-browser-boot
```

**Can current Flintstone boot unchanged in v86? NO.**

There are two independent blockers:

1. The current x86 target is an x86-64 Linux process, not a firmware-bootable
   kernel. `make baremetal` selects real port-I/O drivers, but still links the
   shell through the host C/C++ runtime, `pthread`, SQLite, OpenSSL, and the
   ELF program interpreter. There is no reset entry, 16-bit loader, freestanding
   linker script, Multiboot header, EFI image, MBR, or bootloader handoff.
2. The hardware-facing assembly is genuinely 64-bit. The GDT has a 64-bit code
   descriptor, the IDT uses 16-byte long-mode gates and `iretq`, and the ATA,
   port-I/O, allocator, stack, and interrupt paths use the AMD64 ABI and
   64-bit registers. v86 deliberately omits x86-64 extensions.

The repository is therefore mixed:

- The normal and `baremetal` Make targets build an x86-64 hosted ELF on x86
  hosts (or a hosted AArch64 ELF for `ARCH=arm`).
- The hardware driver implementations contain real x86-64 port-I/O and
  long-mode routines, but are entered from hosted `main`, not firmware.
- `VM/` emulates a small 16/32-bit x86 subset. It starts a synthetic
  `MOV`/`OUT`/`HLT` demonstration at `07c0:0000`; it does not execute the
  Flintstone shell/kernel ELF.

An i386 browser target is not currently a small packaging change. It would
require a deliberate freestanding kernel boundary plus i386 entry, ABI, GDT,
IDT, paging, allocator/stack, interrupt, and ATA assembly implementations.
Until that work is explicitly approved, preserve the x86-64 implementation
and use **Path B**: first create a real x86-64 boot image, then select a browser
emulator that implements long mode.

## Traced execution paths

Current `make baremetal` path:

```text
Linux ELF interpreter -> C runtime _start -> userland/shell/sh.c main()
  -> hosted filesystem/session/thread setup -> drivers_init()
  -> gdt_install()/idt_install() -> x86-64 VGA/PS2/PIT/PIC/ATA drivers
```

The privileged table and port operations cannot run as a normal Linux process.
The `baremetal` name currently means “select hardware driver branches,” not
“produce a bootable kernel.”

Current in-process VM path:

```text
hosted shell -> vm_boot() -> vm_host_create()
  -> copy synthetic bytes to 0x7c00 -> CS:IP 07c0:0000
  -> limited 16/32-bit decoder -> VGA text memory / emulated ports -> HLT
```

Do not nest this VM in v86. A future real kernel image should be independently
consumable by native QEMU and a browser x86-64 emulator.

## Hardware compatibility matrix

| Area | Current Flintstone assumption | v86 virtual hardware | Result |
|---|---|---|---|
| CPU | x86-64 System V code and long-mode GDT/IDT | Pentium-4-era 32-bit x86; no x86-64 | **Blocked** |
| Bootloader | None for the current ELF | SeaBIOS/Bochs BIOS can boot normal media | **Blocked** |
| VGA | Text buffer at `0xB8000`; CRT ports `0x3D4/0x3D5` | VGA/SVGA | Compatible after boot exists |
| Keyboard | PS/2 Set-1, data `0x60`, status `0x64` | 8042/PS/2 | Compatible |
| PIT | 8254 channel 0, ports `0x40/0x43`, IRQ0 at 100 Hz | 8254 PIT | Compatible |
| PIC | Dual 8259 at `0x20/0x21`, `0xA0/0xA1` | Dual 8259 PIC | Compatible; current IDT is long-mode-only |
| IDE | ATA PIO primary channel `0x1F0`–`0x1F7`, LBA28 | IDE controller | Compatible in principle |
| RTC | No x86 CMOS/RTC driver is wired | CMOS RTC exists | Emulator available; guest support absent |
| Paging | No firmware boot paging path; in-process VM models 32-bit CR0/CR3 paging | 32-bit paging, no long mode | Current x86-64 path blocked |
| Serial | Data writes to COM1 `0x3F8`; no complete early UART init/marker | Emulated serial | Partial; add NS16550 init and marker |
| Network | Bare-metal lab paths are not a proven NE2000 guest driver | NE2000/selected virtio | Not required for first boot |

Known incompatible instructions/features include 64-bit register/ABI use,
RIP-relative addressing, `pushq`/`popq`, `lretq`, `iretq`, 64-bit IDT gates,
and a GDT code descriptor with `L=1`. v86 also does not provide the long-mode
control-register/MSR transition required to enter that mode.

## RAM, BIOS, and artifact

`VM/devices/vm_mem.h` gives the synthetic in-process guest 16 MiB. That is not
evidence for the freestanding kernel's minimum. The honest values are:

- Current boot-boundary allocation: **64 MiB** (not yet minimized).
- Initial x86-64 lab recommendation: **64 MiB**, then measure and reduce.
- Existing synthetic VM allocation: **16 MiB**.

The current additive build command is:

```sh
make browser-kernel
```

It creates:

```text
dist/
├── flintstone.img
└── build-info.json
```

The image contains the new freestanding boot boundary. A fresh build is
intentionally marked `bootable: false` and `v86Compatible: false`; only the
QEMU probe may assert bootability for that manifest.
`contracts/virtualization/contract_p8_browser_artifact.h` defines the normative
`build-info.json` field names, JSON types, ownership, digest, QEMU boot-mode,
validation-outcome, and fail-closed promotion rules. The current manifest uses
schema version `2` and `qemuBootMode: "ide-drive"`.
Test the contract and bounded QEMU boot with:

```sh
make test-browser-kernel
make test-browser-kernel-gate
```

The implemented BIOS/boot chain is:

```text
SeaBIOS-compatible PC BIOS -> Flintstone MBR loader
  -> establish x86-64 long mode -> Flintstone freestanding entry
```

## Boot-success contract

After memory, GDT, IDT, PIC, PIT, VGA, keyboard, and block-driver initialization
succeeds, the kernel emits exactly:

```text
FLINTSTONE_KERNEL_BOOT_OK
```

over initialized NS16550-compatible COM1. VGA remains independent kernel output.
QEMU CI captures `-serial stdio`; a browser emulator captures its serial event.
The marker is declared implemented, but the build never treats that declaration
as an independent boot observation.

## Three separate layers

1. **Flintstone:** C/ASM kernel, memory, scheduler, filesystem, and hardware
   drivers. No DOM, JavaScript, React, browser storage, `window`, or `document`.
2. **Virtual machine:** QEMU or an x86-64-capable browser emulator providing PC
   RAM, VGA, PS/2, PIT, PIC, IDE, RTC, serial, and port I/O.
3. **Lab UI:** static HTML/JavaScript, lifecycle controls, metadata, emulator
   screen, optional serial console, and iframe integration.

Keyboard focus belongs to layer 3 and the emulator:

```text
keyboard -> browser event -> emulator PS/2 -> Flintstone PS/2 driver
```

Display and disk remain virtual hardware paths:

```text
Flintstone VGA writes -> virtual VGA -> emulator screen container
Flintstone filesystem -> block driver -> ATA PIO -> virtual IDE -> raw image
```

Persistence, snapshots, Boot/Pause/Resume/Reset/Power-off, and VM recreation are
lab/emulator lifecycle features. They require no browser API in Flintstone.

## Static lab consumption contract

The minimal `tools/browser-lab/` harness reads metadata first. It refuses to
instantiate v86 while `v86Compatible` or `bootable` is false. Once an i386
bootable image exists, a v86 configuration would use its current API:

```js
const emulator = new V86({
    wasm_path: config.wasm,
    screen_container: document.getElementById("screen"),
    bios: { url: config.bios },
    vga_bios: { url: config.vgaBios },
    hda: { url: `${artifactBase}/${info.artifact}?v=${info.shortCommit}` },
    memory_size: info.recommendedRamBytes,
    autostart: true,
});
```

For the selected Path B, the replacement emulator must offer the equivalent
static JavaScript/WebAssembly API and all of:

- x86-64 long mode and the instructions listed above;
- SeaBIOS-compatible boot or the selected x86-64 boot protocol;
- VGA text memory and CRT ports;
- 8042 PS/2, 8254 PIT, dual 8259 PIC, primary IDE ATA PIO, and COM1;
- at least 64 MiB guest RAM;
- raw disk loading by relative/configurable URL;
- keyboard focus, lifecycle controls, and serial-byte capture;
- operation on a static host without a server runtime.

Do not select an emulator merely because it runs x86-64 user programs; it must
boot a virtual PC and expose these devices.

## CI and deployment

`.github/workflows/browser-kernel-artifact.yml` runs on `main` pushes and manual
dispatch. Today it:

1. builds and tests the hosted and in-process VM paths;
2. builds the freestanding raw disk candidate;
3. runs a bounded native-QEMU IDE boot probe and requires the serial marker;
4. runs the pinned QEMU WebAssembly runtime in Chromium and requires the same
   complete serial marker, verified disk digest, VGA text capture, and lifecycle
   checks;
5. validates schema-versioned metadata and artifact SHA-256;
6. packages the browser runtime, image, manifest, and validation evidence only
   after both independent boot observations succeed.

The workflow contains a fail-closed promotion gate. Upload as
`flintstone-browser-kernel` requires all three independent signals:
`bootable: true`, `browserCompatible: true`, a native-QEMU smoke-test step, and
a browser-QEMU-Wasm test that both actually observe `FLINTSTONE_KERNEL_BOOT_OK`
on serial. The manifest's `bootSuccessMarkerImplemented` declaration is
validated but cannot self-attest either observation.

The future lab deployment workflow should download only that validated artifact,
copy the image and JSON to its static `/artifacts/` directory, and deploy Pages:

```text
validated workflow artifact -> static lab /artifacts/
  -> GitHub Pages -> permanent iframe on bailey-forbes.com
```

The lab fetches `/artifacts/build-info.json`, then loads
`/artifacts/<artifact>?v=<shortCommit>`. Stable paths simplify deployment; the
commit query busts browser/CDN caches. Asset roots are configurable or relative,
so the lab may live at `bpforbes.github.io/<lab>/` and be embedded cross-origin:

```html
<iframe src="https://bpforbes.github.io/<lab>/" title="Flintstone Kernel Lab"></iframe>
```

The lab host must permit framing by `bailey-forbes.com` through its effective
`Content-Security-Policy frame-ancestors` policy. The portfolio keeps a permanent
iframe URL and needs no routine update. After the missing freestanding boot path
and a compatible emulator are delivered, normal Flintstone pushes can compile,
test, boot-smoke, and atomically publish the newest successful image; failed
commits leave the previously deployed image untouched.
