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

## Platform HALs

| Platform | HAL Path | Implements |
|----------|----------|------------|
| x86-64 | `kernel/arch/x86_64/hal/` | ioport (wraps port_io) |
| AArch64 | `kernel/arch/aarch64/hal/` | ioport stubs |
| VM | `VM/hal/` | (future) virtual devices |

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
