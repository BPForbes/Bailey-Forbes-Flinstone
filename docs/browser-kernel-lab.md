# Flintstone browser-kernel contract

This document covers `BPForbes/Bailey-Forbes-Flinstone` kernel artifacts and
the static browser lab. Portfolio iframe markup and parent headers are
specified in [`docs/portfolio-iframe-integration.md`](./portfolio-iframe-integration.md).

## Compatibility decision

### Current freestanding boundary

`make browser-kernel` now builds `dist/flintstone.img`: a raw BIOS disk whose
MBR loads a freestanding payload, constructs identity-mapped long-mode page
tables, installs a 64-bit GDT, and enters `kernel_entry`. The entry owns its
stack, clears `.bss`, installs an early IDT, initializes COM1, writes the
diagnostic VGA cell `F`, and only then emits the boot marker. After the marker
it runs a serial/PS/2 lab shell with in-memory identity and up to four
concurrent sessions (`login`, `su`, `logout`, `whoami`, `session`). That is
still not the hosted ELF port: filesystem, P3 networking, and `server host/join`
remain unavailable, and lab accounts are not the SQLite `fl_users.db` store.

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

The remaining v86 blocker is CPU architecture: v86 deliberately omits x86-64
long mode. `dist/flintstone.img` now supplies a BIOS MBR, freestanding linker
boundary, page tables, and initialized COM1 path and boots in the selected
long-mode-capable QEMU WebAssembly runtime. The normal shell and `baremetal`
targets remain hosted ELF processes and are not firmware boot artifacts.

The hardware-facing assembly is genuinely 64-bit. The GDT has a 64-bit code
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

An i386 v86 target is not a small packaging change. It would require separate
i386 entry, ABI, GDT, IDT, paging, allocator/stack, interrupt, and ATA assembly
implementations. Preserve the x86-64 implementation and the implemented
**Path B**: use the real x86-64 boot image with the pinned browser emulator that
implements long mode.

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
| Bootloader | `dist/flintstone.img` has a BIOS MBR loader; hosted ELF targets have none | SeaBIOS/Bochs BIOS can boot normal media | Image compatible; hosted ELF blocked |
| VGA | Text buffer at `0xB8000`; CRT ports `0x3D4/0x3D5` | VGA/SVGA | Compatible after boot exists |
| Keyboard | PS/2 Set-1, data `0x60`, status `0x64` | 8042/PS/2 | Compatible |
| PIT | 8254 channel 0, ports `0x40/0x43`, IRQ0 at 100 Hz | 8254 PIT | Compatible |
| PIC | Dual 8259 at `0x20/0x21`, `0xA0/0xA1` | Dual 8259 PIC | Compatible; current IDT is long-mode-only |
| IDE | ATA PIO primary channel `0x1F0`–`0x1F7`, LBA28 | IDE controller | Compatible in principle |
| RTC | No x86 CMOS/RTC driver is wired | CMOS RTC exists | Emulator available; guest support absent |
| Paging | The BIOS image builds identity-mapped long-mode page tables; the in-process VM models 32-bit CR0/CR3 paging | 32-bit paging, no long mode | BIOS image remains blocked in v86 |
| Serial | The BIOS image initializes NS16550-compatible COM1 at `0x3F8` and emits the exact boot marker | Emulated serial | Compatible |
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

After IDT, PIC, PIT, VGA, and keyboard initialization succeed, the kernel emits
exactly:

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
keyboard -> browser event -> QEMU send-key -> PS/2 -> Flintstone keyboard driver
```

Display remains a virtual hardware path:

```text
Flintstone VGA writes -> virtual VGA text memory -> emulator canvas
```

The hosted filesystem, block driver, and `server` path are **not** present in
the freestanding browser image. Persistence, snapshots, Boot/Pause/Resume/
Reset/Power-off, and VM recreation are lab/emulator lifecycle features.

## Static lab consumption contract

The minimal `tools/browser-lab/` harness reads and validates metadata first. It
uses schema-2 `browserCompatible` for ordinary boot eligibility; the explicit
validation path additionally accepts a schema-2 `bootableCandidate`. The
implemented **Path B** QEMU WebAssembly adapter offers:

- x86-64 long mode and the instructions listed above;
- SeaBIOS-compatible boot or the selected x86-64 boot protocol;
- VGA text memory and CRT ports;
- 8042 PS/2, 8254 PIT, dual 8259 PIC, primary IDE ATA PIO, and COM1;
- at least 64 MiB guest RAM;
- raw disk loading by relative/configurable URL;
- keyboard focus, lifecycle controls, serial-byte capture, and lab identity
  with concurrent sessions;
- operation on a static host without a server runtime.

Do not select an emulator merely because it runs x86-64 user programs; it must
boot a virtual PC and expose these devices.

## CI and deployment

`.github/workflows/browser-kernel-artifact.yml` runs on pull requests, `main`
pushes, and manual dispatch. Pull requests validate only; they never replace
the published lab. On `main` it:

1. builds and tests the hosted and in-process VM paths;
2. builds the freestanding raw disk candidate;
3. runs a bounded native-QEMU IDE boot probe and requires the serial marker;
4. drives the serial lab shell for `whoami`, concurrent sessions, and switch user;
5. runs the pinned QEMU WebAssembly runtime in Chromium and requires the same
   complete serial marker, verified disk digest, VGA first-cell `F`/`0x07`,
   lifecycle checks, and switch-user sessions;
6. packages the browser runtime, image, manifest, and validation evidence;
7. re-runs Chromium against the packaged `./artifacts/` tree and a second origin
   that iframes the lab.

The workflow contains a fail-closed promotion gate. Upload as
`flintstone-browser-kernel` requires all three independent signals:
`bootable: true`, `browserCompatible: true`, a native-QEMU smoke-test step, and
a browser-QEMU-Wasm test that both actually observe `FLINTSTONE_KERNEL_BOOT_OK`
on serial. The manifest's `bootSuccessMarkerImplemented` declaration is
validated but cannot self-attest either observation.

On `main` only, the same workflow deploys that packaged tree to GitHub Pages.
Pull requests never publish. The configured permanent URL is
`https://bpforbes.github.io/Bailey-Forbes-Flinstone/`. GitHub Pages cannot set
COOP/COEP/CSP; see [`docs/portfolio-iframe-integration.md`](./portfolio-iframe-integration.md).
The lab fetches `./artifacts/build-info.json`, then
`./artifacts/<artifact>?v=<shortCommit>`. Failed commits leave the previously
deployed image untouched.
