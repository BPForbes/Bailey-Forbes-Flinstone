# NASM-ABI-001 — Callee-saved register corruption in `malloc_nolock`

| Field | Value |
|-------|-------|
| **Severity** | High |
| **Component** | Allocator (NASM x86-64) |
| **Status** | Open (report only) |

## Summary

`malloc_nolock` uses **callee-saved** registers `rbx`, `r12`, and `r13` as long-lived locals for the free-list walk and heap growth path, but does **not** save or restore them before `ret`. That violates the [System V AMD64 ABI](https://wiki.osdev.org/System_V_ABI): the callee must preserve `rbx`, `rbp`, `r12`, `r13`, `r14`, and `r15`.

## Affected symbols

- `malloc_nolock` — primary defect

## Affected files (same logic)

| Path | Notes |
|------|-------|
| `arch/x86_64/nasm/alloc_core.asm` | **Built** when `ARCH=x86_64_nasm` |
| `x86-64 (NASM)/alloc/alloc_core.asm` | Legacy mirror |
| `kernel/arch/x86_64/asm/alloc_core.asm` | Kernel tree copy |

## Evidence

After `align16`, the function assigns:

```nasm
mov r12, rax          ; aligned size
xor r13, r13          ; prev free block
mov rbx, [rel free_head]
```

The `.find_fit` / `.no_fit` loop continues to use `rbx`, `r12`, and `r13` until return. There is **no** `push`/`pop` (or equivalent) prologue/epilogue for those registers anywhere in the function.

Relevant region (canonical tree):

```76:131:arch/x86_64/nasm/alloc_core.asm
malloc_nolock:
    test rdi, rdi
    jz .ret0
    call align16
    mov r12, rax
    xor r13, r13
    mov rbx, [rel free_head]
.find_fit:
    ...
.return_ptr:
    lea rax, [rbx + HDR_SIZE]
    ret
.ret0:
    xor rax, rax
    ret
```

## Impact

- Any **direct** call to `malloc_nolock` from C/C++ (or asm that does not save `rbx`/`r12`/`r13` first) can corrupt the caller’s preserved registers.
- **Indirect** impact on `calloc` and `realloc` in the same NASM module: they call `malloc_nolock` and then still use `r12`/`r13` as live values (see **NASM-ABI-002**).
- Symptoms: random crashes, wrong pointers, corrupted locals, unstable nested calls under `USE_ASM_ALLOC` / `ARCH=x86_64_nasm`.

## Recommended fix (do not implement in this report)

At entry, save every callee-saved register the body uses; restore before `ret`. Example pattern:

```nasm
push rbx
push r12
push r13
; ... body ...
pop r13
pop r12
pop rbx
ret
```

Alternatively, refactor to use only caller-saved registers (`rax`, `rcx`, `rdx`, `rsi`, `rdi`, `r8`–`r11`) and document the clobber set if the symbol remains exported.

## Verification ideas

- Small C harness under `ARCH=x86_64_nasm` that sets known values in `rbx`/`r12`/`r13`, calls `malloc_nolock` (with lock held or via public `malloc`), and asserts registers unchanged.
- Compare behavior with GAS build (`arch/x86_64/gas/alloc/alloc_core.s` has the same pattern).
