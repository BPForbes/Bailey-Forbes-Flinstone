/**
 * alloc.h - C declarations for thread-safe ASM malloc/calloc/realloc/free
 *
 * Implemented in NASM x86-64 assembly (alloc_core.asm, alloc_malloc.asm, alloc_free.asm).
 * Uses brk(2) for heap growth, first-fit free list, spinlock for thread safety.
 *
 * Build with: make USE_ASM_ALLOC=1
 *
 * Note: When enabled, these replace the system allocator for the entire process.
 * Some programs may need libc allocator compatibility; disable (USE_ASM_ALLOC=0)
 * if you encounter stability issues.
 */
#ifndef ALLOC_H
#define ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* ALLOC_H */
