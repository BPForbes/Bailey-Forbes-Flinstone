#ifndef MEM_ASM_H
#define MEM_ASM_H

#include <stddef.h>
#include <string.h>

/**
 * Wasm / Emscripten: mem primitives are implemented in arch/wasm/host_stubs.c
 * (memcpy/memset-backed). Same contracts as the ASM versions.
 */
void asm_mem_copy(void *dst, const void *src, size_t n);
void asm_mem_zero(void *ptr, size_t n);
void asm_block_fill(void *ptr, unsigned char byte, size_t n);

#ifndef MEM_ASM_DEBUG
static inline void mem_asm_checked_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}
static inline void mem_asm_checked_zero(void *ptr, size_t n) {
    asm_mem_zero(ptr, n);
}
static inline void mem_asm_checked_fill(void *ptr, unsigned char byte, size_t n) {
    asm_block_fill(ptr, byte, n);
}
#else
#include <assert.h>
static inline void mem_asm_checked_copy(void *dst, const void *src, size_t n) {
    assert(dst != NULL && src != NULL);
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
