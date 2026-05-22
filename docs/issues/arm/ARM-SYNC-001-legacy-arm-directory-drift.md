# ARM-SYNC-001 — Legacy `ARM/` directory drifts from canonical `arch/arm/gas/`

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Status** | Open (report only) |

## Summary

The repository keeps a top-level **`ARM/`** tree that resembles the AArch64 layout but is **not** selected by `make ARCH=arm`. The Makefile uses **`arch/arm/gas/`** exclusively.

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

## Recommended fix (do not implement here)

- Declare **`arch/arm/gas/`** sole source of truth in docs (see `docs/ARCH.md`).
- Remove `ARM/` or add CI drift check / one-way sync from canonical.
