/**
 * ARM generic timer via CNTVCT_EL0 - real implementation.
 */
#include "arm_timer.h"

uint64_t arm_timer_tick_count(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}
