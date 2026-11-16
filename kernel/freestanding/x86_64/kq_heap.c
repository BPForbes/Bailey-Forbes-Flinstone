/*
 * The KeyQuorum embedder's heap (see kq_heap.h): a first-fit free list with coalescing over one arena.
 * Every freed block is zeroed, as the library asks. No hardware access, so tests/test_fs_bootmap.c
 * runs it on the host over a buffer of its own.
 */
#include "fl/mem_asm.h"
#include "kq_heap.h"

#include <stddef.h>
#include <stdint.h>

#define KQ_ALIGN FL_KQ_HEAP_ALIGN

typedef struct kq_blk {
    size_t size; /* payload bytes after this header */
    int used;
    struct kq_blk *next;
} kq_blk_t;

#define HDR ((size_t)((sizeof(kq_blk_t) + KQ_ALIGN - 1) & ~(size_t)(KQ_ALIGN - 1)))

static kq_blk_t *s_head;
static size_t s_heap_size;

int fl_kq_heap_init(uint64_t base, uint64_t size)
{
    if (s_head || (base & (KQ_ALIGN - 1)) || size < FL_KQ_HEAP_MIN || size > UINT64_MAX - base)
        return -1;
    s_head = (kq_blk_t *)(uintptr_t)base;
    s_heap_size = (size_t)size;
    s_head->size = s_heap_size - HDR;
    s_head->used = 0;
    s_head->next = 0;
    return 0;
}

static void heap_split_tail(kq_blk_t *b, size_t need)
{
    kq_blk_t *t = (kq_blk_t *)((uintptr_t)b + HDR + need);

    t->size = b->size - need - HDR;
    t->used = 0;
    t->next = b->next;
    b->size = need;
    b->next = t;
}

/* Payload pointer aligned to `align` (<= 4096): the block's payload is over-sized so an aligned
 * address with a back-pointer to the block header fits inside it. */
void *fl_kq_heap_alloc(size_t len, size_t align)
{
    kq_blk_t *b;
    size_t need;

    if (!s_head)
        return 0; /* no arena from the boot map: fail closed */
    if (align < KQ_ALIGN)
        align = KQ_ALIGN;
    if (align > 4096 || (align & (align - 1)) || len > s_heap_size)
        return 0;
    need = (len + align + sizeof(void *) + KQ_ALIGN - 1) & ~(size_t)(KQ_ALIGN - 1);
    for (b = s_head; b; b = b->next) {
        if (!b->used && b->size >= need) {
            uintptr_t raw = (uintptr_t)b + HDR + sizeof(void *);
            uintptr_t p = (raw + align - 1) & ~(uintptr_t)(align - 1);

            if (b->size >= need + HDR + KQ_ALIGN)
                heap_split_tail(b, need);
            b->used = 1;
            ((kq_blk_t **)p)[-1] = b;
            return (void *)p;
        }
    }
    return 0;
}

void fl_kq_heap_free(void *ptr)
{
    kq_blk_t *b;

    if (!ptr)
        return;
    b = ((kq_blk_t **)ptr)[-1];
    asm_mem_secure_zero((void *)((uintptr_t)b + HDR), b->size); /* freed memory is zeroed */
    b->used = 0;
    for (b = s_head; b; b = b->next)
        while (!b->used && b->next && !b->next->used) {
            b->size += HDR + b->next->size;
            b->next = b->next->next;
        }
}

size_t fl_kq_heap_usable(const void *ptr)
{
    const kq_blk_t *b = ((kq_blk_t *const *)ptr)[-1];

    return b->size - ((uintptr_t)ptr - ((uintptr_t)b + HDR));
}
