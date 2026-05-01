/**
 * ARM generic timer (CNTVCT) - real tick source.
 */
#ifndef ARM_TIMER_H
#define ARM_TIMER_H

#include <stdint.h>

uint64_t arm_timer_tick_count(void);

#endif /* ARM_TIMER_H */
