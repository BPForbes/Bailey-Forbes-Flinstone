#ifndef FL_FREESTANDING_KQ_HEAP_H
#define FL_FREESTANDING_KQ_HEAP_H
/* The KeyQuorum embedder's heap. Every allocation fails until fl_kq_heap_init accepts an arena. */
#include <stddef.h>
#include <stdint.h>

/* Every block is aligned to at least this. */
#define FL_KQ_HEAP_ALIGN 16u

/* Smallest arena: sealing an account key (Argon2id, 19 MiB) needs more than 19 MiB at once. */
#define FL_KQ_HEAP_MIN (40ull << 20)

/* Returns 0, or -1 (already set, base not 16-byte aligned, smaller than FL_KQ_HEAP_MIN, or wrapping). */
int fl_kq_heap_init(uint64_t base, uint64_t size);

/* Returns len bytes aligned to align (a power of two up to 4096, at least FL_KQ_HEAP_ALIGN), or NULL. */
void *fl_kq_heap_alloc(size_t len, size_t align);
/* Zeroes the block (asm_mem_secure_zero) and returns it to the free list. NULL is ignored. */
void fl_kq_heap_free(void *ptr);
/* Returns the bytes usable from ptr (a live fl_kq_heap_alloc result) to the end of its block. */
size_t fl_kq_heap_usable(const void *ptr);

#endif
