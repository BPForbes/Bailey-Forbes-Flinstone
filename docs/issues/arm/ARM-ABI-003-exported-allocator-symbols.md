# ARM-ABI-003 — Exported allocator internals are public linker symbols

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Status** | **Fixed** |

## Summary

`arch/arm/gas/alloc_core.s` exports `malloc_nolock`, `lock_acquire`, `lock_release`, `init_heap_once_nolock`, `push_free`, and `unlink_free` as **`.globl`** symbols. Comments state “MUST hold lock” for some entry points, but the linker still exposes them to C/C++ like ordinary functions.

## Risk

- Direct calls from future C code bypassing `malloc`/`free` locking discipline.
- ABI expectations apply to every exported symbol ([ARM-ABI-001](ARM-ABI-001-free-callee-saved.md) for `free`; `malloc_nolock` itself is compliant in canonical tree).

## Resolution (implemented)

Same as NASM-ABI-003: **`.hidden`** on internals in `alloc_core.s`, plus **`scripts/linker/alloc_internal_local.ver`** on **`USE_ASM_ALLOC=1`** links so only **`malloc`**, **`calloc`**, **`realloc`**, **`free`** are global in the final binary.

## Files

- `arch/arm/gas/alloc_core.s`
- Legacy mirrors: `ARM/alloc/alloc_core.s`, `kernel/arch/aarch64/asm/alloc_core.S`
