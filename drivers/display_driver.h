#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "driver_types.h"

/* Create display driver.
 * Host: printf to stdout.
 * BAREMETAL: write to VGA memory 0xB8000. */
display_driver_t *display_driver_create(void);
void display_driver_destroy(display_driver_t *drv);

#endif /* DISPLAY_DRIVER_H */
