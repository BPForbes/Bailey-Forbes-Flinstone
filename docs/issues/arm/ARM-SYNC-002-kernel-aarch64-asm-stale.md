# ARM-SYNC-002 — `kernel/arch/aarch64/asm/` allocator copies are stale

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Status** | **Fixed** (PR #154) |
| **Related** | [ARM-SYNC-001](ARM-SYNC-001-legacy-arm-directory-drift.md) |

## Summary

**Historical:** `kernel/arch/aarch64/asm/alloc_*.S` had drifted from legacy **`ARM/alloc/`** and did not match corrected **`arch/arm/gas/`**.

**Current:** PR #154 copies **`arch/arm/gas/alloc_core.s`**, **`alloc_malloc.s`**, and **`alloc_free.s`** into **`kernel/arch/aarch64/asm/`** (allocator ABI fixes, 80-byte `free` frame, `.hidden` symbols). **`MIGRATION_GUIDE.md`** documents **`arch/arm/gas/`** as canonical with kernel copies synced.

## Resolution

Regenerated kernel allocator asm from **`arch/arm/gas/`**; keep them in sync when changing canonical sources.
