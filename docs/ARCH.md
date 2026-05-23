# Architecture Layout

The project supports multiple assembly backends for memory primitives, port I/O, and allocator.

## Supported Architectures

| ARCH | Assembly | Compiler | Notes |
|------|----------|----------|-------|
| `x86_64_gas` | GAS (GNU as) | gcc | Default. AT&T syntax. |
| `x86_64_nasm` | NASM | gcc | Intel syntax. Requires `nasm`. |
| `arm` | GAS (AArch64) | aarch64-linux-gnu-gcc | ARM64/Linux. Port I/O stubs (no x86 in/out). |

## Build

```bash
# Default: x86_64 GAS
make

# NASM (requires: apt install nasm)
make ARCH=x86_64_nasm

# ARM64 cross-compile (requires: apt install gcc-aarch64-linux-gnu)
make ARCH=arm
```

## Layout

```
arch/
  x86_64/
    gas/           # GAS/AT&T: mem_asm.s, port_io.s, alloc/*.s
    nasm/          # NASM: mem_asm.asm, port_io.asm, alloc/*.asm
  arm/
    gas/           # AArch64: mem_asm.s, port_io.s (stubs), alloc/*.s
```

## API Parity

All variants expose the same C-callable symbols:

- **mem_asm**: `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill`
- **port_io**: `port_inb`, `port_outb`, `port_inw`, `port_outw` (ARM: stubs return 0/no-op)
- **alloc** (USE_ASM_ALLOC=1): `malloc`, `calloc`, `realloc`, `free`

## Drivers

All architectures use the same drivers and unified I/O stack (see docs/IO_STACK.md):

- **block_driver**: host file I/O; bare-metal would use AHCI/NVMe (MMIO)
- **keyboard_driver**: host stdin / bare-metal port 0x60 (x86 PS/2)
- **display_driver**: host printf / bare-metal VGA (MMIO at 0xB8000)
- **timer_driver**: host sleep / bare-metal PIT (port 0x40)
- **pic_driver**: host no-op / bare-metal 8259 (port 0x20/0xA0)
- **pci**: config space; x86 uses port 0xCF8/0xCFC or ECAM; ARM uses ECAM

On ARM bare-metal, port_io is stubbed; MMIO and PCI (ECAM) work.
