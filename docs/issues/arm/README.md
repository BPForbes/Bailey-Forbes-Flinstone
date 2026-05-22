# AArch64 (ARM) GAS assembly — open issue reports

Audit date: 2026-05-22  
Scope: **AArch64 GAS** for `ARCH=arm`, legacy **`ARM/`** mirror, and related **`kernel/arch/aarch64/asm/`** copies.  
**No fixes** in this directory — reports only.

## Build vs legacy paths

| Role | Path |
|------|------|
| **Canonical (Makefile `ASMSRCS_*`)** | `arch/arm/gas/` |
| **Legacy mirror (do not build)** | `ARM/` (`ARM/alloc/`, `ARM/drivers/`, `ARM/mem_asm.s`) |
| **Kernel tree copies** | `kernel/arch/aarch64/asm/` (allocator files **do not** match canonical) |
| **Not ARM** | Repo-root `alloc/` and `mem_asm.s` are **x86-64 GAS**, not AArch64 |

Build: `make ARCH=arm` (requires AArch64 cross toolchain; see **AGENTS.md**).

## Summary vs NASM audit

| Area | Canonical `arch/arm/gas` | Legacy `ARM/` |
|------|--------------------------|---------------|
| `malloc_nolock` callee-saved | **Preserves** `x19`–`x25` (prologue/epilogue) | **Broken** — uses `x19`–`x22` without save |
| `calloc` / `realloc` after `malloc_nolock` | **OK** (core restores callee-saved) | **At risk** (same clobber pattern as NASM legacy) |
| `free` coalesce path | **Issue** — uses `x21`–`x25` without save ([ARM-ABI-001](ARM-ABI-001-free-callee-saved.md)) | Saves `x19`–`x22`; different code |

## Issue index

| ID | Severity | Title | Primary file(s) |
|----|----------|-------|-----------------|
| [ARM-ABI-001](ARM-ABI-001-free-callee-saved.md) | **High** | `free` clobbers callee-saved `x21`–`x25` | `arch/arm/gas/alloc_free.s` |
| [ARM-ABI-002](ARM-ABI-002-legacy-malloc_nolock-callee-saved.md) | **High** | Legacy `malloc_nolock` — no callee-saved save | `ARM/alloc/alloc_core.s` |
| [ARM-LOGIC-001](ARM-LOGIC-001-legacy-malloc-return-pointer.md) | **High** | Legacy `malloc_nolock` wrong pointer on `.Luse_whole` | `ARM/alloc/alloc_core.s` |
| [ARM-LOGIC-002](ARM-LOGIC-002-legacy-heap_end-brk.md) | **High** | Legacy `malloc_nolock` does not store `brk` result in `heap_end` | `ARM/alloc/alloc_core.s` |
| [ARM-SYNC-001](ARM-SYNC-001-legacy-arm-directory-drift.md) | Medium | Legacy `ARM/` drifts from `arch/arm/gas/` | `ARM/` vs `arch/arm/gas/` |
| [ARM-SYNC-002](ARM-SYNC-002-kernel-aarch64-asm-stale.md) | Medium | `kernel/arch/aarch64/asm/` allocator ≠ canonical | `kernel/arch/aarch64/asm/*.S` |
| [ARM-SYNC-003](ARM-SYNC-003-root-alloc-is-x86.md) | Low | Repo-root `alloc/` is x86 GAS, not ARM | `alloc/` |
| [ARM-ABI-003](ARM-ABI-003-exported-allocator-symbols.md) | Medium | Exported allocator internals lack documented ABI | `arch/arm/gas/alloc_core.s` |

## Files reviewed — no separate report

| File | Notes |
|------|-------|
| `arch/arm/gas/mem_asm.s` | `asm_mem_*` — scratch regs `x3`–`x6` / `x5` only |
| `arch/arm/gas/fl_stack_asm.s` | Returns in `w0`/`x0`; scratch `x3`–`x6` |
| `arch/arm/gas/port_io.s` | Stubs; `w0` return |
| `arch/arm/gas/disk_host_io.s` | Syscall wrappers |
| `arch/arm/gas/shell_history_host_asm.s` | `history_asm_append_record`; `x7`–`x11` scratch |
| `arch/arm/gas/alloc_core.s` | `malloc_nolock` **correct** callee-saved handling |
| `arch/arm/gas/alloc_malloc.s` | `malloc` / `calloc` / `realloc` save `x19`+ / `x30` |

## Cross-architecture

x86-64 NASM allocator issues: [../nasm/README.md](../nasm/README.md).  
x86-64 GAS `malloc_nolock` under `arch/x86_64/gas/alloc/` still lacks callee-saved saves (parity gap with fixed AArch64 core).
