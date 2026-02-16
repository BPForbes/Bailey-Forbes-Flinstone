#ifndef TIMER_DRIVER_H
#define TIMER_DRIVER_H

#include "driver_types.h"

/* Create timer driver.
 * Host: usleep/gettimeofday.
 * BAREMETAL: PIT (port 0x40-0x43). */
timer_driver_t *timer_driver_create(void);
void timer_driver_destroy(timer_driver_t *drv);

#endif /* TIMER_DRIVER_H */
