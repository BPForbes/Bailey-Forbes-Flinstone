# Flinstone VM / Hypervisor Implementation Plan

Emulation-based VM that launches when the C executable runs. ASM used for all memory management and port-related data flow.

---

## Phase 1: Core Emulator Skeleton

### 1.1 VM Module Structure

| File | Purpose |
|------|---------|
| `vm/vm.h` | Public API: `vm_boot()`, `vm_run()`, `vm_stop()` |
| `vm/vm_cpu.h`, `vm/vm_cpu.c` | vCPU state: GPRs, segment regs, EFLAGS, RIP; fetch/execute loop |
| `vm/vm_mem.h`, `vm/vm_mem.c` | Guest RAM ops — all via `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill` |
| `vm/vm_decode.h`, `vm/vm_decode.c` | x86 opcode decoder (start with minimal subset) |

### 1.2 Minimal Opcode Subset (v1)

- MOV (reg-reg, reg-mem, mem-reg, imm)
- ADD, SUB, PUSH, POP
- IN, OUT (dispatch to port emulation)
- JMP, JZ, JNZ, JMP short
- INT, IRET
- HLT (stop VM)
- NOP

### 1.3 Guest RAM

- Allocate with `mem_domain_alloc(MEM_DOMAIN_USER, GUEST_RAM_SIZE)` (e.g. 16 MB)
- Init: `asm_mem_zero(guest_ram, GUEST_RAM_SIZE)`
- All guest load/store: bounds check, then `asm_mem_copy` to/from `guest_ram`

### 1.4 Main Integration

- Add `vm_boot()` call in `main()` when VM mode (`-Virtualization -y` or similar)
- After drivers/VRT/VFS init, run VM before or instead of shell

---

## Phase 2: Port I/O Emulation

### 2.1 VM Port Layer

| File | Purpose |
|------|---------|
| `vm/vm_io.h`, `vm/vm_io.c` | Port dispatch table; all data movement via `asm_mem_copy` |

### 2.2 Port Mappings

| Port Range | Virtual Device | Backend | Data Flow (ASM) |
|------------|----------------|---------|-----------------|
| 0x1f0–0x1f7 | IDE primary | `block_driver` + VFS | `asm_mem_copy` sector buf ↔ guest RAM |
| 0x60, 0x64 | Keyboard | `keyboard_driver` | byte in/out |
| 0x3d4, 0x3d5, 0x3c0 | VGA | `display_driver` or guest VGA buffer | `asm_mem_copy` / `asm_block_fill` |
| 0x3f8 | Serial | new `serial_driver` or printf | byte in/out |

### 2.3 IDE Emulation Data Flow

- **Guest IN from 0x1f0**: block_driver read → temp sector buffer → `asm_mem_copy(temp, guest_dma_buf, 512)`
- **Guest OUT to 0x1f0**: `asm_mem_copy(guest_dma_buf, temp, 512)` → block_driver write
- All sector buffers cleared with `asm_mem_zero`

### 2.4 VGA in Guest RAM

- Reserve region in guest RAM at 0xb8000 (size 80×25×2)
- On guest write to 0xb8000: use `asm_mem_copy` or direct byte write; optionally mirror to `display_driver`
- Or: on each frame, `asm_mem_copy(guest_ram + 0xb8000, vga_buf, size)` then render via display_driver

---

## Phase 3: Loader and Bootstrap

### 3.1 Guest Loader

| File | Purpose |
|------|---------|
| `vm/vm_loader.h`, `vm/vm_loader.c` | Load binary from VFS or embedded; use `asm_mem_copy` into guest RAM |

### 3.2 Bootstrap Options

- **Option A**: 512-byte bootsector at 0x7c00; entry RIP = 0x7c00
- **Option B**: Flat binary kernel at 0x100000; entry RIP from header or fixed
- Guest binary: embedded (`.rodata`) or loaded via `vfs_host_create` / `vfs_memory_create`

### 3.3 Minimal Guest Binary

- Stage 1: Print "Flinstone VM" to 0xb8000, then HLT
- Or: Tiny bootblock that loads a second stage

---

## Phase 4: Extended Instruction Set

### 4.1 Additional Opcodes (v2)

- CMP, TEST, AND, OR, XOR
- CALL, RET
- LOOP, LOOPZ
- MOVS, STOS, LODS (string ops — use `asm_mem_copy` for REP)
- INC, DEC
- Conditional jumps (JA, JB, JE, etc.)

### 4.2 String Instructions

- `REP MOVSB` → loop with `asm_mem_copy` (chunked; respect overlap rules)
- `REP STOSB` → `asm_block_fill`
- `REP LODSB` → loop with `asm_mem_copy` (src → register)

---

## Phase 5: Virtual Device Wiring

### 5.1 Block Device

- Create virtual disk via `vfs_host_create` or `vfs_memory_create`
- `block_driver` reads/writes sectors; VM layer uses `asm_mem_copy` for guest↔sector transfers
- IDE registers: base 0x1f0, data 0x1f0, sector 0x1f3, etc.

### 5.2 Keyboard

- `keyboard_driver->poll_scancode`; on guest IN 0x60, return scancode
- No bulk memory; single-byte in/out

### 5.3 Display

- Map guest 0xb8000–0xb8fa0 to guest RAM
- Periodic or on-write: `asm_mem_copy(guest_ram + 0xb8000, display_refresh_buf, 4000)` then display_driver update
- Or: guest writes directly to region; we render that region

---

## Phase 6: Makefile and Build

### 6.1 New Sources

```
vm/vm_cpu.c
vm/vm_mem.c
vm/vm_decode.c
vm/vm_io.c
vm/vm_loader.c
```

### 6.2 Dependencies

- `mem_asm.s`, `mem_domain.c`, `dir_asm.c`
- `block_driver`, `keyboard_driver`, `display_driver`
- `vfs.c`, `vrt.c`
- `drivers/port_io.s` (for bare-metal host port access, if any)

### 6.3 Configuration

- `VM_ENABLE=1` or `-DVM_ENABLE` to include VM code
- Optional: `VM_GUEST_PATH` or embedded guest binary path

---

## Phase 7: Testing

### 7.1 Unit Tests

- `vm_mem`: guest RAM read/write via ASM; bounds checks
- `vm_decode`: decode MOV, IN, OUT, HLT
- `vm_io`: port dispatch, no real I/O

### 7.2 Integration

- Boot minimal guest (HLT after writing to 0xb8000)
- Verify display output or serial output
- Test block I/O: guest reads sector, compare with known data

---

## ASM Usage Checklist

All of the following MUST use ASM memory primitives (no `memcpy`/`memset` in hot path):

- [ ] Guest RAM init
- [ ] Guest binary load
- [ ] Guest memory load/store (emulated)
- [ ] IDE sector read (disk → guest)
- [ ] IDE sector write (guest → disk)
- [ ] VGA buffer refresh (guest RAM → display)
- [ ] String ops (REP MOVS, STOS, LODS)
- [ ] Any buffer clear or fill

---

## Order of Implementation

1. **Phase 1**: vm_cpu, vm_mem, vm_decode; minimal opcodes; vm_boot() stubbed
2. **Phase 2**: vm_io; port dispatch; ASM data flow for IDE
3. **Phase 3**: vm_loader; load guest at 0x7c00; HLT loop works
4. **Phase 4**: Extended opcodes; string ops with asm_mem_*
5. **Phase 5**: Wire block_driver, keyboard_driver, display_driver
6. **Phase 6**: Makefile, build flags, integration
7. **Phase 7**: Tests, minimal guest, validation

---

## File Layout (Proposed)

```
vm/
  vm.h
  vm_cpu.c
  vm_cpu.h
  vm_mem.c
  vm_mem.h
  vm_decode.c
  vm_decode.h
  vm_io.c
  vm_io.h
  vm_loader.c
  vm_loader.h
docs/
  VM_IMPLEMENTATION_PLAN.md
```
