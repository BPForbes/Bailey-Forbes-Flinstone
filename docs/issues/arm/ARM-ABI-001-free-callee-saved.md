# ARM-ABI-001 — `free` clobbers callee-saved `x21`–`x25`

| Field | Value |
|-------|-------|
| **Severity** | High |
| **ABI** | AArch64 AAPCS64 |
| **Component** | Allocator (`ARCH=arm`) |
| **Status** | **Fixed** |

## Summary

**Historical:** In an earlier tree, `free` saved only **`x19`**, **`x20`**, and **`x30`** while the coalesce / free-list walk used **`x21`–`x25`** without restoring them before `ret`.

**Current (fixed):** Canonical **`arch/arm/gas/alloc_free.s`** saves and restores **`x19`–`x25`** and **`x30`** in an 80-byte 16-byte-aligned frame before `ret`. Under AAPCS64, callers may rely on callee-saved registers across `free()`.

## Affected file (built)

- `arch/arm/gas/alloc_free.s`

## Evidence

Prologue saves `x19`/`x20` only:

```17:20:arch/arm/gas/alloc_free.s
free:
    str     x30, [sp, #-48]!
    stp     x19, x20, [sp, #16]
```

Later paths use `x21`–`x25` for `next`, `heap_end`, free-list cursor, and `prev` without matching `stp`/`ldp`:

```32:52:arch/arm/gas/alloc_free.s
    add     x21, x19, #HDR_SIZE
    add     x21, x21, x20            /* next = block + HDR_SIZE + size */
    ...
    mov     x24, xzr                /* prev */
    ...
    ldr     x25, [x10]              /* p = free_head */
.Lsearch:
    ...
    mov     x24, x25
    ldr     x25, [x25, #8]
```

Epilogue restores only `x19`/`x20` and `x30`:

```67:70:arch/arm/gas/alloc_free.s
.Ldone_fast:
    ldp     x19, x20, [sp, #16]
    ldr     x30, [sp], #48
    ret
```

## Impact

- Undefined behavior when C/C++ code calls `free()` and relies on `x21`–`x25` (compiler spill slots) after return.
- Hard-to-reproduce crashes or wrong control flow in optimized builds under `USE_ASM_ALLOC=1` and `ARCH=arm`.

## Note on legacy `ARM/`

`ARM/alloc/alloc_free.s` **does** push/pop `x21`/`x22` in addition to `x19`/`x20`, but still uses **`x23`–`x25`** without saving in some paths — treat legacy as a separate mirror, not the built implementation.

## Recommended fix (do not implement here)

Extend the prologue to save every callee-saved GPR used in the function (`x21`–`x25` at minimum), or refactor to caller-saved registers only.

## Verification ideas

- C test under `make ARCH=arm USE_ASM_ALLOC=1` that sets known values in `x21`–`x25` (inline asm clobbers list) before `free(p)`, then asserts unchanged after return.
