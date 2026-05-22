# ARM-SYNC-003 — Repo-root `alloc/` is x86-64 GAS, not AArch64

| Field | Value |
|-------|-------|
| **Severity** | Low (documentation / navigation) |
| **Status** | Open (report only) |

## Summary

The directory **`alloc/`** at the repository root contains **`alloc_core.s`** and related files using **x86-64** instructions (`movq`, `rdi`, `SYS_brk equ 12`). It is **not** an ARM legacy directory.

AArch64 allocator sources for production builds live only under **`arch/arm/gas/alloc_*.s`**.

## Impact

Contributors searching for “ARM alloc” may open **`alloc/`** by mistake and draw wrong conclusions about AArch64 behavior.

## Recommended fix (do not implement here)

Add a short `alloc/README` or redirect note pointing to `arch/arm/gas/` and `arch/x86_64/gas/alloc/`.
