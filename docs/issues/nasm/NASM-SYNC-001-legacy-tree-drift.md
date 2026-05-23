# NASM-SYNC-001 — Legacy `x86-64 (NASM)/` tree drifts from canonical `arch/x86_64/nasm/`

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Component** | Repository layout / NASM maintenance |
| **Status** | **Closed** (legacy tree removed; canonical `arch/x86_64/nasm/`) |

## Summary

The repository maintains two NASM-oriented directory layouts:

- **`arch/x86_64/nasm/`** — wired in root `Makefile` (`ASMSRCS_BASE`, `ASMSRCS_ALLOC`, `ARCH=x86_64_nasm`).
- **`x86-64 (NASM)/`** — older path referenced in docs and bug reports; **not** the same file set as the canonical tree.

A directory compare shows divergent structure and content (allocator files live under `x86-64 (NASM)/alloc/` vs flat `arch/x86_64/nasm/alloc_*.asm`; canonical tree adds `fl_stack_asm.asm`, `usb_xhci_mmio_asm.asm`, extra `port_inl`/`port_outl`, `.note.GNU-stack`, etc.).

## Impact

- Bug reports and fixes applied only to `x86-64 (NASM)/` will **not** affect `make ARCH=x86_64_nasm` until ported to `arch/x86_64/nasm/`.
- Reviewers may think the legacy tree is authoritative when it is a **mirror/documentation** path.
- **NASM-ABI-001** and **NASM-ABI-002** reproduce in **both** trees today, but fixes must land in the canonical path (and optionally sync or retire the legacy tree).

## Recommended fix (do not implement in this report)

- Document a single source of truth in `docs/ARCH.md` (already points at `arch/x86_64/nasm/`).
- Either delete the legacy tree, symlink, or add a CI check that fails on unintended drift between mirrors.
