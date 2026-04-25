/**
 * Unified timer driver.
 *
 * Host mode:      usleep / static counter (safe for VM and test runs)
 * x86_64 bare-metal:
 *   tick_count  - IRQ0/PIT-driven coarse tick counter, with RDTSC fallback
 *                 before the IRQ0 handler is registered
 *   msleep      - RDTSC-based busy-wait (1 GHz conservative lower bound;
 *                 accurate enough for drivers; a TSC calibration pass can
 *                 refine this later)
 *   PIT init    - programs channel 0 at ~100 Hz (mode 2 / rate-generator)
 *                 and x86_irq0_handler increments impl->ticks for legacy
 *                 software tick semantics
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
#include "fl/mm.h"
#include "fl/mem_asm.h"
#ifndef DRIVERS_BAREMETAL
#include <unistd.h>
#endif

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
    volatile uint64_t ticks; /* incremented by IRQ0 handler */
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
#include "boot/idt.h"

static timer_impl_t *s_irq0_impl;

static void x86_irq0_handler(void) {
    if (s_irq0_impl)
        s_irq0_impl->ticks++;
}

static uint64_t hw_tick_count(timer_driver_t *drv) {
    (void)drv;
    if (s_irq0_impl)
        return s_irq0_impl->ticks;

    uint32_t lo, hi;
    /* RDTSC: reads the time-stamp counter into EDX:EAX.
     * "volatile" prevents the compiler from reordering or eliminating
     * the read; "memory" clobber ensures prior stores are visible. */
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static uint64_t hw_tsc_hz_from_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0x15u), "c"(0u)
                     : "memory");
    if (eax != 0u && ebx != 0u) {
        if (ecx != 0u)
            return ((uint64_t)ecx * (uint64_t)ebx) / (uint64_t)eax;
        /* ECX not enumerated: assume 24 MHz nominal crystal (Intel convention). */
        return (24000000ULL * (uint64_t)ebx) / (uint64_t)eax;
    }
    /* Unknown CPU: assume 2 GHz so we sleep at least as long as requested. */
    return 2000000000ULL;
}

static uint64_t hw_tsc_hz(void) {
    static uint64_t hz;
    if (hz == 0u)
        hz = hw_tsc_hz_from_cpuid();
    return hz;
}

static void hw_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
    uint64_t start = ((uint64_t)hi << 32) | (uint64_t)lo;
    uint64_t hz    = hw_tsc_hz();
    uint64_t wait  = ((uint64_t)ms * hz) / 1000ULL;
    if (wait == 0u && ms != 0u)
        wait = 1u;
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
     *   - Divisor 11931 → IRQ0 fires at ~100 Hz.
     * x86_irq0_handler consumes the IRQ0 stream and increments impl->ticks. */
    fl_ioport_out8(PIT_CMD,      PIT_CMD_CH0_RATE);
    fl_ioport_out8(PIT_CH0_DATA, (uint8_t)(PIT_100HZ_DIV & 0xFFu));
    fl_ioport_out8(PIT_CH0_DATA, (uint8_t)((PIT_100HZ_DIV >> 8) & 0xFFu));
}

static void hw_register_irq0(timer_impl_t *impl) {
    s_irq0_impl = impl;
    /* idt_install must run before hw_register_irq0 installs x86_irq0_handler. */
    if (x86_idt_register_handler(0x20, x86_irq0_handler) != 0)
        s_irq0_impl = (void*)0;
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
    timer_impl_t *impl = (timer_impl_t *)kmalloc(sizeof(*impl));
    if (!impl) return NULL;
    asm_mem_zero(impl, sizeof(*impl));
#ifdef DRIVERS_BAREMETAL
    impl->base.tick_count = hw_tick_count;
    impl->base.msleep     = hw_msleep;
#if defined(__x86_64__) || defined(__i386__)
    hw_pit_init();
    hw_register_irq0(impl);
#endif
#else
    impl->base.tick_count = host_tick_count;
    impl->base.msleep     = host_msleep;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void timer_driver_destroy(timer_driver_t *drv) {
    kfree(drv);
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
