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
#include "core/sys/spinlock.h"

/* Bitfield: one bit per frame, rounded up to cover any partial word. */
#define PMM_BITS_PER_WORD  (sizeof(s_bitmap[0]) * 8u)
#define PMM_WORDS          ((PMM_NUM_FRAMES + (sizeof(unsigned long) * 8u) - 1u) / (sizeof(unsigned long) * 8u))

#define SPINLOCK_INIT 0

static unsigned long s_bitmap[PMM_WORDS]; /* zero-initialised by BSS */
static volatile int s_pmm_lock = SPINLOCK_INIT;

static void bm_set(size_t frame) {
    size_t word = frame / PMM_BITS_PER_WORD;
    size_t bit = frame % PMM_BITS_PER_WORD;
    s_bitmap[word] |= ((unsigned long)1 << bit);
}

static void bm_clear(size_t frame) {
    size_t word = frame / PMM_BITS_PER_WORD;
    size_t bit = frame % PMM_BITS_PER_WORD;
    s_bitmap[word] &= ~((unsigned long)1 << bit);
}

/* Reserve a physical address range by marking all covered frames as allocated.
 * Call during early boot before any allocations to protect kernel/reserved regions. */
void pmm_reserve_range(uintptr_t base, size_t bytes) {
    if (bytes == 0 || base + bytes <= PMM_PHYS_BASE)
        return;
    /* Clamp range to tracked region */
    uintptr_t start = (base < PMM_PHYS_BASE) ? PMM_PHYS_BASE : base;
    uintptr_t end = base + bytes;
    uintptr_t region_end = PMM_PHYS_BASE + PMM_PHYS_MEM;
    if (end > region_end)
        end = region_end;
    if (start >= end)
        return;
    /* Compute first and last frame indices */
    size_t first_frame = (start - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    size_t last_frame = (end - 1 - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    if (last_frame >= PMM_NUM_FRAMES)
        last_frame = PMM_NUM_FRAMES - 1;
    /* Mark all frames in range as allocated */
    for (size_t f = first_frame; f <= last_frame; f++)
        bm_set(f);
}

uintptr_t pmm_alloc_frame(void) {
    spinlock_acquire(&s_pmm_lock);
    for (size_t w = 0; w < PMM_WORDS; w++) {
        if (s_bitmap[w] == ~0UL)
            continue;
        /* __builtin_ctzl: count trailing zeros -> index of first free bit */
        size_t bit   = (size_t)__builtin_ctzl(~s_bitmap[w]);
        size_t frame = w * PMM_BITS_PER_WORD + bit;
        /* If frame index is out of range, continue to next word unless
         * this is the last word (in which case no more frames exist). */
        if (frame >= PMM_NUM_FRAMES) {
            if (w == PMM_WORDS - 1u) {
                spinlock_release(&s_pmm_lock);
                return 0;
            }
            continue;
        }
        bm_set(frame);
        spinlock_release(&s_pmm_lock);
        return PMM_PHYS_BASE + frame * PMM_FRAME_SIZE;
    }
    spinlock_release(&s_pmm_lock);
    return 0;
}

void pmm_free_frame(uintptr_t phys_addr) {
    if (phys_addr < PMM_PHYS_BASE)
        return;
    size_t frame = (phys_addr - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    if (frame >= PMM_NUM_FRAMES)
        return;
    spinlock_acquire(&s_pmm_lock);
    bm_clear(frame);
    spinlock_release(&s_pmm_lock);
}

size_t pmm_free_count(void) {
    spinlock_acquire(&s_pmm_lock);
    size_t free = 0;
    for (size_t w = 0; w < PMM_WORDS; w++) {
        unsigned long bits = ~s_bitmap[w];
        if (w == PMM_WORDS - 1u) {
            size_t rem = PMM_NUM_FRAMES % PMM_BITS_PER_WORD;
            if (rem != 0u)
                bits &= (((unsigned long)1 << rem) - 1u);
        }
        free += (size_t)__builtin_popcountl(bits);
    }
    spinlock_release(&s_pmm_lock);
    return free;
}
