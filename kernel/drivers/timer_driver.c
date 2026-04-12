/**
 * Unified timer driver.
 *
 * Host mode:      usleep / static counter (safe for VM and test runs)
 * x86_64 bare-metal:
 *   tick_count  - RDTSC (always-available monotonic CPU counter)
 *   msleep      - RDTSC-based busy-wait (1 GHz conservative lower bound;
 *                 accurate enough for drivers; a TSC calibration pass can
 *                 refine this later)
 *   PIT init    - programs channel 0 at ~100 Hz (mode 2 / rate-generator)
 *                 so IRQ0 hardware is ready; impl->ticks is reserved for
 *                 a future IRQ0 handler to increment
 * AArch64 bare-metal:
 *   tick_count  - CNTVCT_EL0 (generic timer virtual count; real hardware)
 *   msleep      - CNTVCT_EL0 + CNTFRQ_EL0 (actual system timer frequency,
 *                 not a hardcoded constant)
 */
#include "drivers.h"
#include "fl/driver/console.h"
#include "fl/driver/ioport.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include <stdlib.h>
#include <unistd.h>

/* PIT (8253/8254) register addresses and programming constants */
#define PIT_CH0_DATA 0x40u   /* channel 0 data port            */
#define PIT_CMD      0x43u   /* mode/command register           */
/* Command: channel 0 | lobyte/hibyte | mode 2 (rate gen) | binary */
#define PIT_CMD_CH0_RATE 0x34u
/* Base frequency of the 8253/8254 oscillator in Hz */
#define PIT_BASE_HZ  1193182UL
/* Target IRQ0 rate: 100 Hz -> divisor = 1193182 / 100 = 11931 */
#define PIT_100HZ_DIV 11931u

typedef struct {
    timer_driver_t base;
    volatile uint64_t ticks; /* incremented by IRQ0 handler when wired up */
} timer_impl_t;

/* ------------------------------------------------------------------ */
/* Host / VM mode                                                       */
/* ------------------------------------------------------------------ */

static uint64_t host_tick_count(timer_driver_t *drv) {
    (void)drv;
    static uint64_t t;
    return t++;
}

static void host_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    usleep(ms * 1000);
}

/* ------------------------------------------------------------------ */
/* x86-64 / i386 bare-metal                                            */
/* ------------------------------------------------------------------ */

#ifdef DRIVERS_BAREMETAL
#if defined(__x86_64__) || defined(__i386__)

static uint64_t hw_tick_count(timer_driver_t *drv) {
    (void)drv;
    uint32_t lo, hi;
    /* RDTSC: reads the time-stamp counter into EDX:EAX.
     * "volatile" prevents the compiler from reordering or eliminating
     * the read; "memory" clobber ensures prior stores are visible. */
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static void hw_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    /* RDTSC-based busy-wait.
     * 1 GHz is a conservative lower bound for TSC frequency on any
     * x86_64 CPU made after ~2005, so 1 ms = 1,000,000 cycles is safe
     * (may sleep slightly longer on slower or variable-frequency TSCs;
     * add a calibration step later if precision matters). */
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    uint64_t start = ((uint64_t)hi << 32) | (uint64_t)lo;
    uint64_t wait  = (uint64_t)ms * 1000000ULL;
    uint64_t cur;
    do {
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
        cur = ((uint64_t)hi << 32) | (uint64_t)lo;
    } while (cur - start < wait);
}

static void hw_pit_init(void) {
    /* Program PIT channel 0:
     *   - Command 0x34: channel 0, lobyte/hibyte access, mode 2
     *     (rate generator), binary counting
     *   - Divisor 11931 → IRQ0 fires at ~100 Hz
     * IRQ0 is not yet handled (no IDT wired), but the hardware is now
     * configured and ready; a future IRQ0 handler can increment
     * impl->ticks to drive scheduler-grade timing. */
    fl_ioport_out8(PIT_CMD,      PIT_CMD_CH0_RATE);
    fl_ioport_out8(PIT_CH0_DATA, (uint8_t)(PIT_100HZ_DIV & 0xFFu));
    fl_ioport_out8(PIT_CH0_DATA, (uint8_t)((PIT_100HZ_DIV >> 8) & 0xFFu));
}

/* ------------------------------------------------------------------ */
/* AArch64 bare-metal                                                   */
/* ------------------------------------------------------------------ */

#elif defined(__aarch64__)
#include "hal/arm_timer.h"

static uint64_t hw_tick_count(timer_driver_t *drv) {
    (void)drv;
    return arm_timer_tick_count(); /* reads CNTVCT_EL0 */
}

static void hw_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    /* Read the actual system-counter frequency from CNTFRQ_EL0 rather
     * than assuming a fixed 24 MHz, so this works on any ARM board. */
    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    uint64_t start = arm_timer_tick_count();
    uint64_t ticks = (freq / 1000ULL) * (uint64_t)ms;
    while (arm_timer_tick_count() - start < ticks)
        ;
}

#endif /* arch */
#endif /* DRIVERS_BAREMETAL */

/* ------------------------------------------------------------------ */
/* Driver lifecycle                                                     */
/* ------------------------------------------------------------------ */

timer_driver_t *timer_driver_create(void) {
    timer_impl_t *impl = (timer_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
    impl->base.tick_count = hw_tick_count;
    impl->base.msleep     = hw_msleep;
#if defined(__x86_64__) || defined(__i386__)
    hw_pit_init();
#endif
#else
    impl->base.tick_count = host_tick_count;
    impl->base.msleep     = host_msleep;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void timer_driver_destroy(timer_driver_t *drv) {
    free(drv);
}

uint32_t timer_driver_caps(void) {
    if (!g_timer_driver) return 0;
#ifndef DRIVERS_BAREMETAL
    return FL_CAP_REAL;
#elif defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
}
