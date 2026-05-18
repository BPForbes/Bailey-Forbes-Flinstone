#include "fl/driver/p4_irq_lifecycle.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define FL_P4_BH_QUEUE_MAX 16

typedef struct {
    int irq;
    fl_irq_bottom_half_fn fn;
    void *ctx;
    int pending;
} fl_p4_bh_slot_t;

static volatile int s_hardirq_depth;
static fl_p4_bh_slot_t s_bh_queue[FL_P4_BH_QUEUE_MAX];
static int s_bh_count;

void fl_irq_lifecycle_init(void) {
    s_hardirq_depth = 0;
    s_bh_count = 0;
    for (int i = 0; i < FL_P4_BH_QUEUE_MAX; i++)
        s_bh_queue[i].pending = 0;
}

void fl_irq_lifecycle_enter_hardirq(void) {
    s_hardirq_depth++;
}

void fl_irq_lifecycle_leave_hardirq(void) {
    if (s_hardirq_depth > 0)
        s_hardirq_depth--;
}

int fl_irq_lifecycle_in_hardirq(void) {
    return s_hardirq_depth > 0;
}

void fl_irq_lifecycle_assert_no_sleep_in_hardirq(void) {
#if !defined(NDEBUG)
    if (s_hardirq_depth > 0) {
        fprintf(stderr, "[p4_irq] sleep/blocking forbidden in hardirq (depth=%d)\n",
                (int)s_hardirq_depth);
        abort();
    }
#endif
    (void)0;
}

int fl_irq_lifecycle_defer_bottom_half(int irq, fl_irq_bottom_half_fn fn, void *ctx) {
    if (!fn || irq < 0)
        return -1;
    for (int i = 0; i < FL_P4_BH_QUEUE_MAX; i++) {
        if (!s_bh_queue[i].pending) {
            s_bh_queue[i].irq = irq;
            s_bh_queue[i].fn = fn;
            s_bh_queue[i].ctx = ctx;
            s_bh_queue[i].pending = 1;
            s_bh_count++;
            return 0;
        }
    }
    return -1;
}

void fl_irq_lifecycle_run_bottom_halves(void) {
    fl_irq_lifecycle_assert_no_sleep_in_hardirq();
    for (int i = 0; i < FL_P4_BH_QUEUE_MAX; i++) {
        if (!s_bh_queue[i].pending)
            continue;
        fl_irq_bottom_half_fn fn = s_bh_queue[i].fn;
        void *ctx = s_bh_queue[i].ctx;
        int irq = s_bh_queue[i].irq;
        s_bh_queue[i].pending = 0;
        if (s_bh_count > 0)
            s_bh_count--;
        if (fn)
            fn(irq, ctx);
    }
}
