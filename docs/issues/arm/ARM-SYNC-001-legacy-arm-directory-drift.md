# ARM-SYNC-001 — Legacy `ARM/` directory drifts from canonical `arch/arm/gas/`

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Status** | **Closed** (legacy tree removed) |

## Summary

**Historical:** The repository once kept a top-level **`ARM/`** tree that resembled the AArch64 layout but was **not** selected by `make ARCH=arm`.

**Current:** The Makefile uses **`arch/arm/gas/`** exclusively. The legacy **`ARM/`** directory was **removed** on branch `cursor/nasm-abi-issue-reports-7cb1` (PR #154).

## Structural differences

| Canonical `arch/arm/gas/` | Legacy `ARM/` |
|---------------------------|---------------|
| Flat `alloc_*.s` | `alloc/` subdirectory |
| `fl_stack_asm.s`, `disk_host_io.s`, `shell_history_host_asm.s` | Absent |
| `port_io.s` (stubs) | `drivers/port_io.s` |
| `malloc_nolock` with callee-saved prologue | Broken legacy core ([ARM-ABI-002](ARM-ABI-002-legacy-malloc_nolock-callee-saved.md)) |

`diff -rq arch/arm/gas ARM` reports multiple **Only in** entries and **differ** on `mem_asm.s` and all allocator sources.

## Impact

- Bug fixes applied only under **`ARM/`** do not affect **`make ARCH=arm`**.
- **MIGRATION_GUIDE.md** still references `ARM/*` as migration sources while copies under `kernel/arch/aarch64/asm/` remain stale ([ARM-SYNC-002](ARM-SYNC-002-kernel-aarch64-asm-stale.md)).
- NASM-style confusion: same class of problem as [NASM-SYNC-001](../nasm/NASM-SYNC-001-legacy-tree-drift.md).

## Resolution

- **`arch/arm/gas/`** is the sole AArch64 asm source of truth (see `docs/ARCH.md`, `MIGRATION_GUIDE.md`).
- Legacy **`ARM/`** removed; use **`arch/arm/gas/`** only.
