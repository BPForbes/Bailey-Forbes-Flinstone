# NASM (x86-64) assembly — issue reports

Parent index: [../README.md](../README.md) · AArch64: [../arm/README.md](../arm/README.md)

Audit date: 2026-05-22. **Fixes landed** on branch `cursor/nasm-abi-issue-reports-7cb1` (see canonical `arch/x86_64/nasm/`).

## Build path

| Role | Path |
|------|------|
| **Canonical (`ARCH=x86_64_nasm`)** | `arch/x86_64/nasm/` |
| **Kernel CMake** | `kernel/arch/x86_64/asm/` (synced from canonical) |

Legacy **`x86-64 (NASM)/`** was **removed** (was an unmaintained mirror).

## Issue status

| ID | Status | Resolution |
|----|--------|------------|
| NASM-ABI-001 | **Fixed** | `malloc_nolock` push/pop `rbx`, `r12`, `r13` in `alloc_core.asm` |
| NASM-ABI-002 | **Fixed** | Resolved by NASM-ABI-001 (`calloc`/`realloc` reuse preserved regs) |
| NASM-ABI-003 | Open | Exported internals still `.global` |
| NASM-SYNC-001 | **Closed** | Legacy tree deleted |
| NASM-LINK-001 | **Closed** | Legacy tree deleted |

Historical detail: see `NASM-ABI-*.md` and `NASM-SYNC-001-*.md` in this directory.

## Files reviewed (no open ABI report)

`mem_asm.asm`, `port_io.asm`, `fl_stack_asm.asm`, `usb_xhci_mmio_asm.asm`

## GAS parity

`arch/x86_64/gas/alloc/alloc_core.s` received the same `malloc_nolock` callee-saved fix as NASM.
