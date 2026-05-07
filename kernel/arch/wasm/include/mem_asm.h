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
/**
 * Copy `n` bytes from `src` to `dst` using the architecture-provided memory copy.
 *
 * In debug builds (`MEM_ASM_DEBUG` defined) this function asserts that `dst` and
 * `src` are non-NULL and that the source and destination ranges do not overlap
 * before performing the copy.
 *
 * @param dst Destination buffer where bytes will be written.
 * @param src Source buffer from which bytes will be read.
 * @param n Number of bytes to copy.
 */
static inline void mem_asm_checked_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}
/**
 * Set `n` bytes at `ptr` to zero using the architecture-specific asm implementation.
 *
 * In debug builds (`MEM_ASM_DEBUG`) this wrapper asserts that `ptr` is not NULL
 * before calling the underlying implementation.
 *
 * @param ptr Pointer to the start of the memory region to zero.
 * @param n   Number of bytes to set to zero.
 */
static inline void mem_asm_checked_zero(void *ptr, size_t n) {
    asm_mem_zero(ptr, n);
}
/**
 * Fill a memory region with the specified byte value.
 *
 * @param ptr Destination memory region to write to; must point to at least `n` bytes.
 * @param byte Byte value to write into the region.
 * @param n Number of bytes to set.
 *
 * In debug builds (`MEM_ASM_DEBUG`), this function asserts that `ptr` is not NULL before performing the write.
 */
static inline void mem_asm_checked_fill(void *ptr, unsigned char byte, size_t n) {
    asm_block_fill(ptr, byte, n);
}
#else
#include <assert.h>
/**
 * Copy `n` bytes from `src` to `dst`.
 *
 * Both pointers must be non-NULL and the source and destination ranges must not overlap.
 *
 * @param dst Destination buffer where bytes will be written.
 * @param src Source buffer to read bytes from.
 * @param n Number of bytes to copy.
 */
static inline void mem_asm_checked_copy(void *dst, const void *src, size_t n) {
    assert(dst != NULL && src != NULL);
    assert((const char *)dst >= (const char *)src + n || (const char *)src >= (const char *)dst + n);
    asm_mem_copy(dst, src, n);
}
/**
 * Zeroes `n` bytes beginning at `ptr`.
 *
 * Asserts that `ptr` is not NULL, then sets the specified memory region to zero.
 *
 * @param ptr Pointer to the start of the memory region to clear; must not be NULL.
 * @param n Number of bytes to set to zero.
 */
static inline void mem_asm_checked_zero(void *ptr, size_t n) {
    assert(ptr != NULL);
    asm_mem_zero(ptr, n);
}
/**
 * Fill a memory region with the specified byte value.
 *
 * Asserts that `ptr` is not NULL, then sets `n` bytes starting at `ptr` to `byte`
 * by calling the architecture-specific `asm_block_fill`.
 *
 * @param ptr Pointer to the start of the memory region; must not be NULL.
 * @param byte Byte value to write into each of the `n` bytes.
 * @param n Number of bytes to fill.
 */
static inline void mem_asm_checked_fill(void *ptr, unsigned char byte, size_t n) {
    assert(ptr != NULL);
    asm_block_fill(ptr, byte, n);
}
#endif

#endif /* MEM_ASM_H */
