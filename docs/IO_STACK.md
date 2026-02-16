# I/O Stack

Unified I/O abstraction for drivers. Tier 1 = preferred for new hardware.

## Tier 1 – MMIO (drivers/mmio.h)

- **Use**: AHCI, NVMe, USB xHCI, APIC, HPET, GOP framebuffers
- **Arch**: x86-64 and ARM
- **API**: `mmio_read8/16/32`, `mmio_write8/16/32(addr, value)`
- Implemented as C `volatile` pointer access

## Tier 1 – PCI Config (drivers/pci.h)

- **Use**: Device discovery, BAR programming
- **x86**: Port 0xCF8/0xCFC (legacy) or ECAM via `pci_set_ecam_base()`
- **ARM**: ECAM only; call `pci_set_ecam_base()` with phys base from firmware/DT
- **Host**: Stubs (no hardware access)
- **API**: `pci_read_config8/16/32`, `pci_write_config8/16/32(bus, dev, fn, offset, value)`

## Tier 2 – Port I/O (drivers/port_io.h)

- **Use**: PS/2 (0x60/0x64), 8259 PIC (0x20/0xA0), PIT (0x40)
- **x86**: Real `in`/`out` via ASM
- **ARM**: Stubs (return 0 / no-op)
- **API**: `port_inb/outb`, `port_inw/outw`, `port_inl/outl`

## Tier 3 – Legacy

- VGA ports, serial (COM), IDE ports
- Niche; not in the main stack

## Driver Consistency

All drivers include `io.h` (which pulls in port_io, mmio, pci). Same code path on both arches:

- **x86**: Port I/O and PCI config work; MMIO works
- **ARM**: Port I/O stubbed; MMIO and PCI (ECAM) work

Future drivers (AHCI, GOP) use MMIO and PCI; legacy drivers (keyboard, PIC, timer) use port I/O.
