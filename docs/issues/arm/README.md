# AArch64 (ARM) GAS assembly — issue reports

Parent index: [../README.md](../README.md) · NASM: [../nasm/README.md](../nasm/README.md)

Audit date: 2026-05-22. **Fixes landed** on branch `cursor/nasm-abi-issue-reports-7cb1`.

## Build path

| Role | Path |
|------|------|
| **Canonical (`ARCH=arm`)** | `arch/arm/gas/` |
| **Kernel CMake** | `kernel/arch/aarch64/asm/` (synced from canonical) |

Legacy **`ARM/`** and repo-root **`alloc/`** (x86 duplicate) were **removed**.

## Issue status

| ID | Status | Resolution |
|----|--------|------------|
| ARM-ABI-001 | **Fixed** | `free` saves/restores `x21`–`x25` in `alloc_free.s` |
| ARM-ABI-002 | **Closed** | Legacy `ARM/` deleted; canonical `malloc_nolock` was already correct |
| ARM-LOGIC-001 | **Closed** | Legacy/kernel stale copies removed or synced |
| ARM-LOGIC-002 | **Closed** | Same |
| ARM-SYNC-001 | **Closed** | `ARM/` deleted |
| ARM-SYNC-002 | **Fixed** | Kernel `asm/*.S` copied from `arch/arm/gas/` |
| ARM-SYNC-003 | **Closed** | Repo-root `alloc/` deleted |
| ARM-ABI-003 | **Fixed** | `.hidden` + `scripts/linker/alloc_internal_local.ver` on `USE_ASM_ALLOC=1` |

Historical detail: see `ARM-*.md` in this directory.

## Canonical allocator notes

- `malloc_nolock` in `alloc_core.s` already preserved `x19`–`x25`.
- `calloc` / `realloc` use `asm_mem_zero` / `asm_mem_copy` after `malloc_nolock` (safe with preserved callee-saved in core).
