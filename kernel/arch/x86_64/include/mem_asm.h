#ifndef MEM_ASM_H
#define MEM_ASM_H

#include <stddef.h>

/**
 * ASM memory primitives (x86-64 NASM) — hot-path disk/cluster ops.
 *
 * CONTRACTS (caller must ensure):
 *
 * asm_mem_copy(dst, src, n):
 *   - dst, src: non-NULL, valid for n bytes
 *   - OVERLAP: memcpy-only, NOT memmove. dst must NOT overlap src.
 *     If dst in [src, src+n) or src in [dst, dst+n): UNDEFINED, data corruption.
 *   - Alignment: no requirement; unaligned accesses handled.
 *   - Clobbered: rax, rcx, r8, r9, r10 (SysV caller-saved). Others preserved.
 *
 * asm_mem_zero(ptr, n):
 *   - ptr: non-NULL, valid for n bytes
 *   - Clobbered: r8, r9, r10.
 *
 * asm_block_fill(ptr, byte, n):
 *   - ptr: non-NULL, valid for n bytes
 *   - Clobbered: r8, r9, r10, rsi (extended).
 *
 * asm_mem_secure_zero(ptr, n):
 *   - ptr: non-NULL, valid for n bytes. Wipes secrets (keys, passwords): the
 *     stores cannot be elided and are fenced before return.
 *   - Clobbered: rdi, rsi.
 *
 * asm_mem_ct_equal(a, b, n):
 *   - a, b: non-NULL, valid for n bytes. Returns 1 when equal, else 0.
 *   - Constant time in n (no early exit): compare digests and signatures.
 *   - Clobbered: rax, rcx, rdx, rdi, rsi.
 *
 * Max size: no explicit limit; avoid single-call sizes > ~2^31 on 32-bit size_t.
 */
void asm_mem_copy(void *dst, const void *src, size_t n);
void asm_mem_zero(void *ptr, size_t n);
void asm_block_fill(void *ptr, unsigned char byte, size_t n);
void asm_mem_secure_zero(void *ptr, size_t n);
int asm_mem_ct_equal(const void *a, const void *b, size_t n);

/* Debug builds: use mem_asm_checked_* for overlap/ptr asserts. */
#ifdef MEM_ASM_DEBUG
#include <assert.h>
static inline void mem_asm_checked_copy(void *dst, const void *src, size_t n) {
    assert(dst != NULL && src != NULL);
    /* Overlap: dst in [src, src+n) or src in [dst, dst+n) => invalid */
    assert((const char *)dst >= (const char *)src + n || (const char *)src >= (const char *)dst + n);
    asm_mem_copy(dst, src, n);
}
static inline void mem_asm_checked_zero(void *ptr, size_t n) {
    assert(ptr != NULL);
    asm_mem_zero(ptr, n);
}
static inline void mem_asm_checked_fill(void *ptr, unsigned char byte, size_t n) {
    assert(ptr != NULL);
    asm_block_fill(ptr, byte, n);
}
#endif

#endif /* MEM_ASM_H */
