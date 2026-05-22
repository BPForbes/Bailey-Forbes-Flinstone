# NASM-ABI-003 — Exported allocator internals are not ABI-safe entry points

| Field | Value |
|-------|-------|
| **Severity** | Medium |
| **Component** | Allocator exports (NASM x86-64) |
| **Status** | Open (report only) |
| **Related** | [NASM-ABI-001](NASM-ABI-001-malloc_nolock-callee-saved.md) |

## Summary

Several allocator helpers are **`global`** and therefore visible to the linker as ordinary functions. Only `malloc`, `calloc`, `realloc`, and `free` are intended libc-style entry points. The others are documented in comments as “must hold lock” internals but still use the **public C symbol namespace** without ABI prologues.

## Exported symbols (canonical `alloc_core.asm`)

| Symbol | ABI concern |
|--------|-------------|
| `malloc_nolock` | Callee-saved clobber (**NASM-ABI-001**) |
| `init_heap_once_nolock` | Appears safe today (uses only `rdi`/`rax`) |
| `lock_acquire` | Spin loop; clobbers `rax`; no callee-saved saves |
| `lock_release` | Minimal |
| `push_free` | Uses `rdi`, `rax`; no callee-saved |
| `unlink_free` | Uses `rdi`, `rsi`, `rax`; no callee-saved |

## Risk

Future C code (or LTO across translation units) may call `malloc_nolock` or `lock_acquire` directly. Even if current C only calls `malloc`/`free`, **linker visibility + ABI violation** is a footgun and breaks interoperability with compiler-generated prologues that assume SysV rules.

## Recommended fix (do not implement in this report)

- Make internal symbols **local** (`.global` → not exported, or static inline C wrappers only), **or**
- Give each exported symbol a full SysV-compliant prologue/epilogue, **or**
- Rename to private symbols (e.g. `malloc_nolock_locked`) and keep a single C-facing `malloc`.

## Files

- `arch/x86_64/nasm/alloc_core.asm`
- `x86-64 (NASM)/alloc/alloc_core.asm`
- `kernel/arch/x86_64/asm/alloc_core.asm`
