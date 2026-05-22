# ARM-SYNC-002 — `kernel/arch/aarch64/asm/` allocator copies are stale

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Status** | Open (report only) |
| **Related** | [ARM-SYNC-001](ARM-SYNC-001-legacy-arm-directory-drift.md) |

## Summary

`kernel/arch/aarch64/asm/alloc_core.S`, `alloc_malloc.S`, and `alloc_free.S` match the **legacy `ARM/alloc/`** style (e.g. `malloc_nolock` without callee-saved prologue, [ARM-LOGIC-001](ARM-LOGIC-001-legacy-malloc-return-pointer.md), [ARM-LOGIC-002](ARM-LOGIC-002-legacy-heap_end-brk.md)), **not** the corrected **`arch/arm/gas/`** sources used by `make ARCH=arm`.

## Impact

- Kernel or tooling that assumes “migrated” AArch64 asm under `kernel/arch/aarch64/asm/` may reintroduce fixed bugs.
- **MIGRATION_GUIDE.md** lists `ARM/alloc/*.s` → `kernel/arch/aarch64/asm/` as moved, but content was not kept in sync with `arch/arm/gas/`.

## Recommended fix (do not implement here)

- Regenerate kernel copies from `arch/arm/gas/alloc_*.s`, or
- Stop duplicating userland allocator asm in the kernel tree and link one implementation.
