# Unified Driver Architecture

One driver API, many backends. Drivers are consistent across x86-64, ARM64, and VM.

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
| x86-64 | `kernel/arch/x86_64/hal/` | ioport (port_io), block_transport_host |
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
2. **Next**: Block driver calls `fl_block_driver_create(hal_transport)`
3. **Next**: `fl_driver_registry_register_all` → `fl_bus_enumerate` → probe → attach
4. **Future**: Virtio as common device model for all platforms
