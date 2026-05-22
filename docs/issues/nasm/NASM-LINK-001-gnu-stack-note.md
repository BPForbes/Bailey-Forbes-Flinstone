# NASM-LINK-001 — Legacy NASM objects missing `.note.GNU-stack`

| Field | Value |
|-------|-------|
| **Severity** | Low |
| **Component** | Toolchain / linking |
| **Status** | Open (report only) |

## Summary

Canonical NASM sources under `arch/x86_64/nasm/` begin with:

```nasm
section .note.GNU-stack progbits alloc noexec
```

Legacy files under `x86-64 (NASM)/` (for example `mem_asm.asm`, `alloc/alloc_core.asm`, `drivers/port_io.asm`) **omit** this note.

## Impact

On some Linux distributions, missing GNU-stack notes on `.o` files assembled from NASM can mark the stack executable unless the linker applies NX policies — usually a **warning or hardening gap**, not the same class of bug as NASM-ABI-001.

## Recommended fix (do not implement in this report)

Add the same `.note.GNU-stack` line to legacy objects if that tree remains, or retire the legacy tree (**NASM-SYNC-001**).
