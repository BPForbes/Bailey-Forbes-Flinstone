#ifndef FL_FREESTANDING_KQ_PLATFORM_H
#define FL_FREESTANDING_KQ_PLATFORM_H
/* The KeyQuorum embedder's heap (kq_platform.c), FL-02: fl_kernel_main hands it the arena it picked
 * from the boot memory map. Until then, and after a refused arena, every allocation fails. */
#include <stdint.h>

/* Sealing an account key (Argon2id, 19 MiB) needs more than 19 MiB at once: no smaller arena. */
#define FL_KQ_HEAP_MIN (40ull << 20)

/* 0, or -1 (already set, base not 16-byte aligned, smaller than FL_KQ_HEAP_MIN, or wrapping). */
int fl_kq_heap_init(uint64_t base, uint64_t size);

#endif
