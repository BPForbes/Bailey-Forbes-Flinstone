#ifndef FL_FREESTANDING_KQ_HEAP_H
#define FL_FREESTANDING_KQ_HEAP_H
#include <stddef.h>
#include <stdint.h>
/* KeyQuorum embedder heap: every allocation fails until fl_kq_heap_init accepts an arena. */

#define FL_KQ_HEAP_ALIGN 16u

#define FL_KQ_HEAP_MIN (40ull << 20)

int fl_kq_heap_init(uint64_t base, uint64_t size);

void *fl_kq_heap_alloc(size_t len, size_t align);
void fl_kq_heap_free(void *ptr);
size_t fl_kq_heap_usable(const void *ptr);

#endif
