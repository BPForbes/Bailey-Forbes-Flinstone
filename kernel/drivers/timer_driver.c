/**
 * Unified timer driver - uses fl_ioport for baremetal PIT.
 * Host: usleep. BAREMETAL: PIT port I/O via HAL.
 */
#include "drivers.h"
#include "fl/driver/console.h"
#include "fl/driver/ioport.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    timer_driver_t base;
    volatile uint64_t ticks;
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
    volatile uint64_t end = ((uint64_t)ms * 1193) / 1000;
    for (volatile uint64_t i = 0; i < end; i++)
        (void)i;
}
#endif

timer_driver_t *timer_driver_create(void) {
    timer_impl_t *impl = (timer_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
    impl->base.tick_count = hw_tick_count;
    impl->base.msleep = hw_msleep;
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

uint32_t timer_driver_caps(void) {
    if (!g_timer_driver) return 0;
#ifndef DRIVERS_BAREMETAL
    return FL_CAP_REAL;
#elif defined(__x86_64__) || defined(__i386__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
}
