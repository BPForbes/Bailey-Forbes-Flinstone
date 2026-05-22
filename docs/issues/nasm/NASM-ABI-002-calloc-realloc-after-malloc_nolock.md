# NASM-ABI-002 — `calloc` / `realloc` use `r12`/`r13` after `malloc_nolock`

| Field | Value |
|-------|-------|
| **Severity** | High |
| **Component** | Allocator wrappers (NASM x86-64) |
| **Status** | Open (report only) |
| **Related** | [NASM-ABI-001](NASM-ABI-001-malloc_nolock-callee-saved.md) |

## Summary

`calloc` and `realloc` in `alloc_malloc.asm` **push** callee-saved registers at entry and store important values in `r12`/`r13`, then **`call malloc_nolock`**, which **clobbers** `rbx`, `r12`, and `r13` (see NASM-ABI-001). After the call, both functions still read **`r12` and/or `r13` from the register bank**, not from the stack slots created by the initial `push` sequence.

`malloc` is largely unaffected because it only needs the size in `r12` **before** the call and moves the result through `rbx` afterward without reusing the pre-call `r12` value.

## Affected files

| Path | Notes |
|------|-------|
| `arch/x86_64/nasm/alloc_malloc.asm` | Canonical build |
| `x86-64 (NASM)/alloc/alloc_malloc.asm` | Legacy mirror (same pattern) |
| `kernel/arch/x86_64/asm/alloc_malloc.asm` | Kernel tree copy |

## `calloc` — evidence

```33:62:arch/x86_64/nasm/alloc_malloc.asm
calloc:
    push rbx
    push r12
    push r13
    ...
    mov r13, rax          ; product size (after mul)
    call lock_acquire
    call init_heap_once_nolock
    mov rdi, r13
    call malloc_nolock    ; clobbers r13 (and rbx, r12)
    mov rbx, rax
    call lock_release
    ...
    mov rcx, r13          ; BUG: r13 no longer holds product size
    xor rax, rax
    mov r12, rcx          ; BUG: propagates garbage into zero loop
```

The correct product size still exists only on the **stack** (from the initial `push r13` of the *caller’s* `r13`, not the computed size). The implementation never reloads the computed byte count from a stack temporary after `malloc_nolock` returns.

**Impact:** `calloc` can zero the wrong number of bytes (under/over-zero), leak uninitialized memory to callers, or walk past the allocation.

## `realloc` — evidence

```75:103:arch/x86_64/nasm/alloc_malloc.asm
realloc:
    push rbx
    push r12
    push r13
    push r14
    ...
    mov r12, rdi          ; original ptr
    mov r13, rsi          ; new size
    mov r14, [rdi-HDR_SIZE]
    and r14, -16
    call lock_acquire
    call init_heap_once_nolock
    mov rdi, r13
    call malloc_nolock    ; clobbers r12, r13
    mov rbx, rax
    call lock_release
    ...
    mov rcx, r14
    cmp r14, r13          ; BUG: r13 not new size
    cmova rcx, r13
    mov rdi, rbx
    mov rsi, r12          ; BUG: r12 not original ptr
    rep movsb
    mov rdi, r12          ; BUG: wrong ptr passed to free()
    call free
```

**Impact:** `realloc` can copy the wrong length, copy from/to wrong addresses, and pass a garbage pointer to `free()` — severe heap corruption and crashes.

## Recommended fix (do not implement in this report)

1. Fix **NASM-ABI-001** so `malloc_nolock` preserves callee-saved registers **or**
2. In `calloc`/`realloc`, spill `size`, `ptr`, and `old_payload` to the stack (or caller-saved registers not touched by `malloc_nolock`) before the call and reload after.

## Verification ideas

- Unit-style test: `calloc(100, 2)` then verify every byte is zero for 200 bytes.
- `realloc` growth/shrink tests with canary bytes in the old block and pointer identity checks before/after `free` path.
