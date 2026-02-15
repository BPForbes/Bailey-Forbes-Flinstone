# VM Guest Contract

Defines how the VM loads and runs guest code.

## Supported Formats

### 1. Flat Kernel (current default)

- **Layout**: Raw binary loaded at fixed address (0x7C00).
- **Entry**: CS=0x07C0, EIP=0 (real-mode linear 0x7C00).
- **Contract**: Guest starts at offset 0; no MBR, no boot sector protocol.
- **Use**: Minimal programs, demos, custom bytecode.

### 2. Boot Sector

- **Layout**: 512-byte sector loaded at 0x7C00.
- **Entry**: Same as flat kernel; guest must relocate if needed.
- **Contract**: First 512 bytes of image; optional BPB at offset 3.
- **Use**: MBR-style loaders, legacy boot code.

### 3. Custom Bytecode (future)

- **Layout**: Custom instruction encoding.
- **Contract**: TBD; would require separate decode path.

## Deterministic Boot Sequence

1. **Reset**: Zero RAM and CPU (asm_mem_zero, vm_cpu_init).
2. **Load**: Copy guest binary to GUEST_LOAD_ADDR (0x7C00).
3. **Entry**: Set CS=VM_BOOT_ENTRY_CS (0x07C0), EIP=VM_BOOT_ENTRY_IP (0).
4. **Run**: Execute from entry; no randomness in boot path.

## Current Implementation

- `vm_loader`: Loads raw binary at `GUEST_LOAD_ADDR` (0x7C00).
- `vm_host`: Embeds minimal guest (MOV/OUT/HLT) or loads from file.
- **Effective contract**: Flat kernel. Guest is position-dependent at 0x7C00.
- No boot sector detection; no partition table parsing.
- Boot uses VM_BOOT_ENTRY_CS, VM_BOOT_ENTRY_IP for deterministic entry.

## Recommendations

- Document guest entry in README: CS:IP = 0x07C0:0000.
- For boot sector support: detect 0x55 0xAA at 510–511, optional.
- For flat kernel: keep current; document expected load address.
