# ARM-LOGIC-001 — Legacy `malloc_nolock` returns wrong block on whole-block path

| Field | Value |
|-------|-------|
| **Severity** | High (legacy / kernel copy) |
| **Status** | Open (report only) |

## Summary

In **`ARM/alloc/alloc_core.s`** (and **`kernel/arch/aarch64/asm/alloc_core.S`**), `.Lreturn_ptr` always computes the return pointer as **`x22 + HDR_SIZE`**. On the **`.Luse_whole`** path, the active block pointer is **`x21`**, not `x22`. `x22` may be stale or refer to a split fragment from an earlier iteration.

## Evidence

```132:164:ARM/alloc/alloc_core.s
.Luse_whole:
    ldr x0, [x21]
    bic x0, x0, #FLAG_FREE
    str x0, [x21]
    b .Lreturn_ptr
...
.Lreturn_ptr:
    add x0, x22, #HDR_SIZE    /* should use x21 on .Luse_whole path */
    ret
```

Canonical `arch/arm/gas/alloc_core.s` uses **`x20`** consistently as the current block and returns `add x0, x20, #HDR_SIZE` — **not affected**.

## Impact

Whole free-block allocations can return pointers into the wrong object → heap corruption under legacy/kernel sources if ever linked.

## Recommended fix (do not implement here)

Return `x21 + HDR_SIZE` on whole-block path, or unify on one block pointer register like canonical code.
