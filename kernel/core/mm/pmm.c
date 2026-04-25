/**
 * Minimal physical frame allocator.
 *
 * Tracks PMM_NUM_FRAMES (1024) physical frames of PMM_FRAME_SIZE (4 KiB)
 * each, starting at physical address PMM_PHYS_BASE (1 MB).  State is a
 * compact bitfield: 1 = allocated, 0 = free.
 *
 * In DRIVERS_BAREMETAL builds, alloc_page() in kmalloc.c routes through
 * pmm_alloc_frame(); host builds use aligned_alloc instead.
 */
#include "pmm.h"

/* Bitfield: 1024 frames / 64 bits per unsigned long = 16 words */
#define PMM_WORDS  (PMM_NUM_FRAMES / (sizeof(unsigned long) * 8u))

static unsigned long s_bitmap[PMM_WORDS]; /* zero-initialised by BSS */

static void bm_set(size_t frame) {
    s_bitmap[frame >> 6u] |= (1UL << (frame & 63u));
}

static void bm_clear(size_t frame) {
    s_bitmap[frame >> 6u] &= ~(1UL << (frame & 63u));
}

uintptr_t pmm_alloc_frame(void) {
    for (size_t w = 0; w < PMM_WORDS; w++) {
        if (s_bitmap[w] == ~0UL)
            continue;
        /* __builtin_ctzl: count trailing zeros → index of first free bit */
        size_t bit   = (size_t)__builtin_ctzl(~s_bitmap[w]);
        size_t frame = (w << 6u) | bit;
        if (frame >= PMM_NUM_FRAMES)
            return 0;
        bm_set(frame);
        return PMM_PHYS_BASE + frame * PMM_FRAME_SIZE;
    }
    return 0;
}

void pmm_free_frame(uintptr_t phys_addr) {
    if (phys_addr < PMM_PHYS_BASE)
        return;
    size_t frame = (phys_addr - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    if (frame >= PMM_NUM_FRAMES)
        return;
    bm_clear(frame);
}

size_t pmm_free_count(void) {
    size_t free = 0;
    for (size_t w = 0; w < PMM_WORDS; w++) {
        unsigned long bits = ~s_bitmap[w];
        while (bits) {
            free++;
            bits &= bits - 1u;
        }
    }
    return free;
}
