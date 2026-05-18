/**
 * **P4-2** IRQ lifecycle enforcement (hardirq vs bottom-half).
 */
#ifndef FL_P4_IRQ_LIFECYCLE_H
#define FL_P4_IRQ_LIFECYCLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fl_irq_bottom_half_fn)(int irq, void *ctx);

void fl_irq_lifecycle_init(void);
void fl_irq_lifecycle_enter_hardirq(void);
void fl_irq_lifecycle_leave_hardirq(void);
int  fl_irq_lifecycle_in_hardirq(void);

/** Debug: abort when called with hardirq depth > 0 (enabled unless NDEBUG). */
void fl_irq_lifecycle_assert_no_sleep_in_hardirq(void);

/** Queue work for after hardirq; runs on **fl_irq_lifecycle_run_bottom_halves**. */
int fl_irq_lifecycle_defer_bottom_half(int irq, fl_irq_bottom_half_fn fn, void *ctx);

/** Drain deferred bottom halves (hosted: call after dispatch or from worker thread). */
void fl_irq_lifecycle_run_bottom_halves(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_IRQ_LIFECYCLE_H */
