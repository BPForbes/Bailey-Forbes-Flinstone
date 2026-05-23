# NASM-PERF-001 — `realloc` growth copy uses `asm_mem_copy` (x86-64)

| Field | Value |
|-------|-------|
| **Severity** | Low (performance) |
| **Component** | Allocator (`realloc` copy path) |
| **Status** | **Fixed** (GitHub #156) |
| **Related** | AArch64 `alloc_malloc.s` (already used `asm_mem_copy`) |

## Summary

x86-64 NASM and GAS `realloc` used `cld; rep movsb` for the live-block copy after `malloc_nolock`. AArch64 used `asm_mem_copy` (qword loop + byte tail). Large reallocations were slower on x86-64 than on ARM for the same algorithm.

## Fix

- `arch/x86_64/gas/alloc/alloc_malloc.s` — `call asm_mem_copy` with `rdi`/`rsi`/`rdx` = dst/src/n.
- `arch/x86_64/nasm/alloc_malloc.asm` — same.
- `kernel/arch/x86_64/asm/alloc_malloc.asm` — kept in sync with NASM canonical tree.

`asm_mem_copy` is defined in `arch/x86_64/gas/mem_asm.s` and `arch/x86_64/nasm/mem_asm.asm` (already linked via `ASMSRCS_BASE`).

## Verification

- `make test_alloc USE_ASM_ALLOC=1` (default GAS and `ARCH=x86_64_nasm`).
- `realloc` growth/shrink with canary bytes (see `tests/test_alloc.c`).
