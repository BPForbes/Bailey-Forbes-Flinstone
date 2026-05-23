# ARM-LOGIC-002 — Legacy `malloc_nolock` fails to update `heap_end` from `brk`

| Field | Value |
|-------|-------|
| **Severity** | High (legacy / kernel copy) |
| **Status** | Open (report only) |

## Summary

On the grow path (`.Lno_fit`), legacy code calls `sys_brk` but then stores the **old** `heap_end` value (`x22` loaded before `brk`) back into `heap_end`, instead of the **return value** of `brk` in `x0`.

## Evidence

```143:160:ARM/alloc/alloc_core.s
.Lno_fit:
    ldr x22, [x0]         /* x22 = old heap_end */
    ...
    bl sys_brk
    ...
    str x22, [x0]         /* BUG: writes pre-brk heap_end */
    str x19, [x22]
```

Canonical code:

```137:150:arch/arm/gas/alloc_core.s
    bl      sys_brk
    cmp     x0, x22
    b.lo    .Lret0_fail
    ...
    str     x0, [x10]     /* heap_end = brk return */
```

## Impact

Subsequent allocations can overlap or reuse VA space; `brk` growth appears to succeed but metadata stays stale.

## Recommended fix (do not implement here)

Store `x0` from `sys_brk` into `heap_end` after a successful comparison, matching `arch/arm/gas/alloc_core.s`.
