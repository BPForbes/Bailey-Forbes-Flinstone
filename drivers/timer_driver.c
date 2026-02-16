/* Timer driver - ticks and sleep.
 * Host: usleep. BAREMETAL: PIT setup, tick counter. */
#include "timer_driver.h"
#include "io.h"
#include <stdlib.h>
#include <unistd.h>

#ifdef DRIVERS_BAREMETAL
#define PIT_CH0    0x40
#define PIT_CTRL   0x43
#define PIT_FREQ   1193182
#endif

typedef struct {
    timer_driver_t base;
    volatile uint64_t ticks;
#ifdef DRIVERS_BAREMETAL
    int pit_initialized;
#endif
} timer_impl_t;

static uint64_t host_tick_count(timer_driver_t *drv) {
    (void)drv;
    static uint64_t t;
    return t++;
}

static void host_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    usleep(ms * 1000);
}

#ifdef DRIVERS_BAREMETAL
static uint64_t hw_tick_count(timer_driver_t *drv) {
    timer_impl_t *impl = (timer_impl_t *)drv->impl;
    return impl->ticks;
}

static void hw_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    /* Simplified: busy wait. Full impl would use PIT interrupt. */
    volatile uint64_t end = ((uint64_t)ms * 1193) / 1000;  /* approx */
    for (volatile uint64_t i = 0; i < end; i++)
        (void)i;
}
#endif

timer_driver_t *timer_driver_create(void) {
    timer_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
    impl->base.tick_count = hw_tick_count;
    impl->base.msleep = hw_msleep;
    /* PIT init would go here: port_outb(PIT_CTRL, 0x34); etc. */
    impl->pit_initialized = 0;
#else
    impl->base.tick_count = host_tick_count;
    impl->base.msleep = host_msleep;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void timer_driver_destroy(timer_driver_t *drv) {
    free(drv);
}
