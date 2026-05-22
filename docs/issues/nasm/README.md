# NASM (x86-64) assembly — open issue reports

Parent index: [../README.md](../README.md) · AArch64 audit: [../arm/README.md](../arm/README.md)

Audit date: 2026-05-22  
Scope: **NASM** sources used for `ARCH=x86_64_nasm` and the legacy mirror tree.  
**No fixes** are included in this directory — reports only.

## Build vs mirror paths

| Role | Path |
|------|------|
| **Canonical (Makefile `ASMSRCS_*`)** | `arch/x86_64/nasm/` |
| **Legacy / documentation mirror** | `x86-64 (NASM)/` (layout differs; not all objects wired the same way) |
| **Kernel CMake NASM copies** | `kernel/arch/x86_64/asm/` (allocator files parallel the legacy tree) |

Run the allocator path with: `make ARCH=x86_64_nasm` (requires `nasm`).

## Issue index

| ID | Severity | Title | Primary file(s) |
|----|----------|-------|-----------------|
| [NASM-ABI-001](NASM-ABI-001-malloc_nolock-callee-saved.md) | **High** | `malloc_nolock` clobbers callee-saved `rbx`, `r12`, `r13` | `arch/x86_64/nasm/alloc_core.asm` |
| [NASM-ABI-002](NASM-ABI-002-calloc-realloc-after-malloc_nolock.md) | **High** | `calloc` / `realloc` reuse `r12`/`r13` after `malloc_nolock` | `arch/x86_64/nasm/alloc_malloc.asm` |
| [NASM-ABI-003](NASM-ABI-003-exported-allocator-symbols-abi.md) | Medium | Exported allocator helpers lack ABI prologue/epilogue | `arch/x86_64/nasm/alloc_core.asm` |
| [NASM-SYNC-001](NASM-SYNC-001-legacy-tree-drift.md) | Medium | Legacy `x86-64 (NASM)/` drifts from canonical `arch/x86_64/nasm/` | both trees |
| [NASM-LINK-001](NASM-LINK-001-gnu-stack-note.md) | Low | Missing `.note.GNU-stack` on legacy NASM objects | `x86-64 (NASM)/` |

## Files reviewed (no issue filed)

These NASM translation units were read for SysV AMD64 callee-saved / argument register use. No separate report was opened:

- `arch/x86_64/nasm/mem_asm.asm` — `asm_mem_copy`, `asm_mem_zero`, `asm_block_fill` (void APIs; only caller-saved scratch registers)
- `arch/x86_64/nasm/port_io.asm` — port I/O (integer return in `eax` / `ax` / `al`)
- `arch/x86_64/nasm/fl_stack_asm.asm` — stack helpers (return in `eax`)
- `arch/x86_64/nasm/usb_xhci_mmio_asm.asm` — MMIO read/write/fence

## Cross-architecture note

The same `malloc_nolock` register pattern exists in `arch/x86_64/gas/alloc/alloc_core.s` (GAS build). GAS is out of scope for the IDs above but should be fixed in the same change set when allocator ABI is corrected.
