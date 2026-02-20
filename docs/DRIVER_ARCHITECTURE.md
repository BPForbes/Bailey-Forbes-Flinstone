# Unified Driver Architecture

One driver API, many backends. Drivers are consistent across x86-64, ARM64, and VM.

## Unified Drivers (`kernel/drivers/`)

| Driver | Path | Uses HAL |
|--------|------|----------|
| Block | `kernel/drivers/block/block_driver.c` | fl_hal_block_transport |
| Block transport (host) | `kernel/drivers/block/block_transport_host.c` | disk_asm |
| Keyboard | `kernel/drivers/keyboard_driver.c` | fl_ioport |
| Display | `kernel/drivers/display_driver.c` | fl_mmio (VGA) |
| Timer | `kernel/drivers/timer_driver.c` | fl_ioport |
| PIC | `kernel/drivers/pic_driver.c` | fl_ioport |

## Canonical Driver API (`kernel/include/fl/driver/`)

| Header | Purpose |
|--------|---------|
| `driver.h` | Lifecycle, registration, fl_driver_ops |
| `bus.h` | Device discovery, fl_device_desc, fl_bus_enumerate |
| `device.h` | fl_device_t handle |
| `irq.h` | Interrupt controller abstraction |
| `ioport.h` | Port I/O (x86; ARM stubs) |
| `mmio.h` | Memory-mapped I/O |
| `block.h` | Block driver interface + HAL transport |
| `console.h` | Display, keyboard, timer, PIC |
| `net.h` | Network (placeholder) |
| `driver_types.h` | Canonical types; legacy aliases |
| `driver_caps.h` | Capability structs |

## Platform HALs and Arch-Specific Drivers

| Platform | HAL Path | PCI | IOPort | ARM MMIO HAL |
|----------|----------|-----|--------|--------------|
| x86-64 | `kernel/arch/x86_64/hal/` | real (CF8/CFC) | real port I/O | - |
| AArch64 | `kernel/arch/aarch64/hal/` | real (ECAM) | stubs | PL011 UART, GIC, ARM timer |
| VM | `VM/hal/` | (future) | - | - |

Both architectures have real implementations. ARM uses MMIO (PL011 UART, GIC, ARM generic timer, PCI ECAM) instead of port I/O.

## Capability Reporting (Real vs Stub)

Drivers report whether they have real hardware paths or are stubs:

- **FL_CAP_REAL** – driver has real implementation; hardware access works
- **FL_CAP_STUB** – driver is placeholder; may return 0xFF, no-ops

| Driver | Host / VM | Baremetal x86 (i386/x64) | Baremetal ARM |
|--------|-----------|---------------------------|---------------|
| Block | REAL (disk file / vm_disk) | REAL | REAL |
| Keyboard | REAL (stdin) | REAL (PS/2 port 0x60) | REAL (PL011 UART) |
| Display | REAL (printf) | REAL (VGA 0xB8000) | REAL (UART) |
| Timer | REAL (usleep) | REAL (PIT port 0x40) | REAL (CNTVCT) |
| PIC | REAL (no-op) | REAL (8259) | REAL (GIC) |
| PCI | stub (host); VM emulates | REAL (CF8/CFC) | REAL (ECAM) |

**API**: `keyboard_driver_caps()`, `display_driver_caps()`, `timer_driver_caps()`, `pic_driver_caps()`, `pci_get_caps()`. Block uses `get_caps()` → `flags` includes FL_CAP_REAL.

**Refuse stub**: Call `drivers_require_real_block()` or `drivers_require_real_pci()` before relying on hardware; returns -1 if stub.

**Report**: `drivers_report_caps()` logs real/stub status at init. In baremetal mode, FATAL if any driver is stub.

**Require real**: `drivers_require_real_block()`, `drivers_require_real_keyboard()`, `drivers_require_real_display()`, `drivers_require_real_timer()`, `drivers_require_real_pic()`, `drivers_require_real_pci()` return 0 if real, -1 if stub. `drivers_all_real_for_vm()` checks kbd/display/timer/pic; VM boot fails if any are stub.

**Probe/Selftest**: `driver_probe_*()` returns 0 if real. `drivers_run_selftest()` runs block read, timer tick, display putchar; exits if any fail. Build gate: `make check-stubs` fails if forbidden stub markers exist.

## Bus Types

- **FL_BUS_PCI** – PCI config space (x86/ARM ECAM)
- **FL_BUS_DT** – Device Tree (ARM)
- **FL_BUS_SYNTH** – Synthetic (host disk, VM)

## Parity Rule

If a driver exists on one platform, it must at least compile on all. Run:

```bash
make parity
```

Requires `aarch64-linux-gnu-gcc` for ARM. Install: `apt install gcc-aarch64-linux-gnu`

## Migration Path

1. **Done**: Canonical headers; arch `driver_types.h` → thin wrappers
2. **Done**: Unified drivers in `kernel/drivers/`; block uses HAL transport; keyboard/display/timer/pic use fl_ioport/fl_mmio
3. **Done**: Deleted duplicate arch-specific drivers (x86_64, aarch64)
4. **Next**: `fl_driver_registry_register_all` → `fl_bus_enumerate` → probe → attach
5. **Future**: Virtio as common device model for all platforms
