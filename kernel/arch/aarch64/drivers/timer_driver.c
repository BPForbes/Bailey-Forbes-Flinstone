/* AArch64 timer driver stub: host uses usleep, baremetal should wire to system timer */
#include "timer_driver.h"
#include "driver_types.h"
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    timer_driver_t base;
    uint64_t ticks;
} timer_impl_t;

static uint64_t host_tick_count(timer_driver_t *drv) {
    (void)drv;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void host_msleep(timer_driver_t *drv, unsigned int ms) {
    (void)drv;
    usleep((useconds_t)ms * 1000);
}

timer_driver_t *timer_driver_create(void) {
    timer_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->base.tick_count = host_tick_count;
    impl->base.msleep = host_msleep;
    impl->base.impl = impl;
    return &impl->base;
}

void timer_driver_destroy(timer_driver_t *drv) {
    free(drv);
}
