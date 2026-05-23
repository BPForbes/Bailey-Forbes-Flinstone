# ARM-ABI-002 — Legacy `malloc_nolock` clobbers `x19`–`x22` (no prologue)

| Field | Value |
|-------|-------|
| **Severity** | High (legacy path only) |
| **ABI** | AArch64 AAPCS64 |
| **Status** | **Closed** (legacy `ARM/` removed; kernel copy synced) |
| **Related** | [ARM-SYNC-001](ARM-SYNC-001-legacy-arm-directory-drift.md), [NASM-ABI-001](../nasm/NASM-ABI-001-malloc_nolock-callee-saved.md) |

## Summary

**`ARM/alloc/alloc_core.s`** (legacy, **not** wired in Makefile) implements `malloc_nolock` using **`x19`–`x22`** without saving them. The **canonical** `arch/arm/gas/alloc_core.s` already uses a proper `stp`/`ldp` prologue — do not confuse the two trees.

## Affected files

| Path | Built for `ARCH=arm`? |
|------|------------------------|
| `ARM/alloc/alloc_core.s` | **No** |
| `kernel/arch/aarch64/asm/alloc_core.S` | **Synced** (kernel copy matches canonical `arch/arm/gas/alloc_core.s`) |

## Evidence (legacy)

```98:105:ARM/alloc/alloc_core.s
malloc_nolock:
    cbz x0, .Lret0
    bl align16
    mov x19, x0           /* callee-saved, not saved */
    mov x20, xzr
    ...
    ldr x21, [x21]        /* x21 = free_head */
```

No `stp x19, x20, [sp, #-…]!` (or equivalent) before use.

## Impact

- Misleading if developers edit **`ARM/`** expecting it to match production `ARCH=arm` behavior (tree removed on PR #154).

## Resolution (PR #154)

- Legacy **`ARM/`** deleted.
- **`kernel/arch/aarch64/asm/alloc_core.S`** synced from **`arch/arm/gas/alloc_core.s`** (callee-saved prologue, `.hidden` BSS, `x30` save in `malloc_nolock` / `init_heap_once_nolock`).
- Production **`make ARCH=arm`** uses canonical **`arch/arm/gas/`** only.
