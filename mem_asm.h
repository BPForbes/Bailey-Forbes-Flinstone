#ifndef MEM_ASM_H
#define MEM_ASM_H

#include <stddef.h>

/**
 * ASM primitives for hot-path memory operations in disk/cluster layer.
 *
 * IMPORTANT: asm_mem_copy is memcpy-only, NOT memmove.
 * Overlapping regions (dst overlaps src) will corrupt data.
 * Use memmove or copy backward for overlap-safe behavior.
 */
void asm_mem_copy(void *dst, const void *src, size_t n);
void asm_mem_zero(void *ptr, size_t n);
void asm_block_fill(void *ptr, unsigned char byte, size_t n);

#endif /* MEM_ASM_H */
