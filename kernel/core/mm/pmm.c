/**
 * Physical frame allocator (P1-4): bitmap tracks reserved/allocated state;
 * free frames are kept on a fixed **fl_stack** for O(1) alloc/free.
 *
 * TODO(contract-mount/GH-122): pmm.h / fl_stack.h anchor contract_p1_pmm.h.
 * TODO(P1/Codex): DRIVERS_BAREMETAL lock-order graph + PMM/arena behavior per
 * docs/ROADMAP.md P1→P2 gate; see docs/p0_p2_pr_coverage.md (integration ⚠️).
 */
#include "pmm.h"
#include "../memory/fl_stack.h"
#include "core/sys/spinlock.h"

#define PMM_BITS_PER_WORD  (sizeof(unsigned long) * 8u)
#define PMM_WORDS          ((PMM_NUM_FRAMES + (PMM_BITS_PER_WORD) - 1u) / (PMM_BITS_PER_WORD))
#define SPINLOCK_INIT 0

static unsigned long s_bitmap[PMM_WORDS];
static uintptr_t     s_free_stack_buf[PMM_NUM_FRAMES];
static fl_stack_t    s_free_stack;
static volatile int  s_pmm_lock = SPINLOCK_INIT;
static int           s_pmm_stack_ready;

static void pmm_ensure_stack(void) {
    size_t f;
    if (s_pmm_stack_ready)
        return;
    fl_stack_init(&s_free_stack, s_free_stack_buf, PMM_NUM_FRAMES);
    for (f = 0; f < PMM_NUM_FRAMES; f++) {
        if (((s_bitmap[f / PMM_BITS_PER_WORD] >> (f % PMM_BITS_PER_WORD)) & 1u) == 0u)
            fl_stack_push(&s_free_stack, f);
    }
    s_pmm_stack_ready = 1;
}

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

static int bm_test(size_t frame) {
    size_t word = frame / PMM_BITS_PER_WORD;
    size_t bit = frame % PMM_BITS_PER_WORD;
    return (int)((s_bitmap[word] >> bit) & 1u);
}

void pmm_reserve_range(uintptr_t base, size_t bytes) {
    uintptr_t start, end, region_end;
    size_t first_frame, last_frame, f;

    if (bytes == 0 || base + bytes <= PMM_PHYS_BASE)
        return;
    start = (base < PMM_PHYS_BASE) ? PMM_PHYS_BASE : base;
    end = base + bytes;
    region_end = PMM_PHYS_BASE + PMM_PHYS_MEM;
    if (end > region_end)
        end = region_end;
    if (start >= end)
        return;
    first_frame = (start - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    last_frame = (end - 1 - PMM_PHYS_BASE) / PMM_FRAME_SIZE;
    if (last_frame >= PMM_NUM_FRAMES)
        last_frame = PMM_NUM_FRAMES - 1;

    spinlock_acquire(&s_pmm_lock);
    for (f = first_frame; f <= last_frame; f++)
        bm_set(f);
    s_pmm_stack_ready = 0;
    spinlock_release(&s_pmm_lock);
}

uintptr_t pmm_alloc_frame(void) {
    uintptr_t phys = 0;
    if (pmm_alloc_frame_result(&phys) == FL_RESULT_OK)
        return phys;
    return 0;
}

fl_result_t pmm_alloc_frame_result(uintptr_t *out_phys) {
    uintptr_t frame;

    if (!out_phys)
        return FL_RESULT_INVAL;

    spinlock_acquire(&s_pmm_lock);
    pmm_ensure_stack();
    if (fl_stack_pop(&s_free_stack, &frame) != 0) {
        spinlock_release(&s_pmm_lock);
        return FL_RESULT_NOMEM;
    }
    bm_set((size_t)frame);
    spinlock_release(&s_pmm_lock);
    *out_phys = PMM_PHYS_BASE + frame * PMM_FRAME_SIZE;
    return FL_RESULT_OK;
}

void pmm_free_frame(uintptr_t phys_addr) {
    (void)pmm_free_frame_result(phys_addr);
}

fl_result_t pmm_free_frame_result(uintptr_t phys_addr) {
    size_t frame;

    if (phys_addr < PMM_PHYS_BASE)
        return FL_RESULT_INVAL;
    frame = (size_t)((phys_addr - PMM_PHYS_BASE) / PMM_FRAME_SIZE);
    if (frame >= PMM_NUM_FRAMES)
        return FL_RESULT_INVAL;

    spinlock_acquire(&s_pmm_lock);
    if (!bm_test(frame)) {
        spinlock_release(&s_pmm_lock);
        return FL_RESULT_INVAL;
    }
    pmm_ensure_stack();
    if (fl_stack_push(&s_free_stack, frame) != 0) {
        s_pmm_stack_ready = 0;
        spinlock_release(&s_pmm_lock);
        return FL_RESULT_ERR;
    }
    bm_clear(frame);
    spinlock_release(&s_pmm_lock);
    return FL_RESULT_OK;
}

size_t pmm_free_count(void) {
    size_t n;
    spinlock_acquire(&s_pmm_lock);
    pmm_ensure_stack();
    n = fl_stack_count(&s_free_stack);
    spinlock_release(&s_pmm_lock);
    return n;
}
