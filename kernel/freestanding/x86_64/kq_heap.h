#ifndef FL_FREESTANDING_KQ_HEAP_H
#define FL_FREESTANDING_KQ_HEAP_H
/* The KeyQuorum embedder's heap (kq_heap.c), FL-02: fl_kernel_main hands it the arena it picked
 * from the boot memory map. Until then, and after a refused arena, every allocation fails. */
#include <stddef.h>
#include <stdint.h>

/* Every block is aligned to at least this. */
#define FL_KQ_HEAP_ALIGN 16u

/* Sealing an account key (Argon2id, 19 MiB) needs more than 19 MiB at once: no smaller arena. */
#define FL_KQ_HEAP_MIN (40ull << 20)

/* 0, or -1 (already set, base not 16-byte aligned, smaller than FL_KQ_HEAP_MIN, or wrapping). */
int fl_kq_heap_init(uint64_t base, uint64_t size);

/* A block of len bytes aligned to align (a power of two up to 4096; FL_KQ_HEAP_ALIGN at least), or NULL. */
void *fl_kq_heap_alloc(size_t len, size_t align);
/* Zeroes the block (asm_mem_secure_zero) and returns it to the free list. NULL is ignored. */
void fl_kq_heap_free(void *ptr);
/* Bytes usable from ptr (a live fl_kq_heap_alloc result) to the end of its block. */
size_t fl_kq_heap_usable(const void *ptr);

#endif
