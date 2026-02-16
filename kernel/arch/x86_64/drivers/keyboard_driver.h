#ifndef KEYBOARD_DRIVER_H
#define KEYBOARD_DRIVER_H

#include "driver_types.h"

/* Create keyboard driver.
 * Host: reads from stdin.
 * BAREMETAL: polls port 0x60 via port_io. */
keyboard_driver_t *keyboard_driver_create(void);
void keyboard_driver_destroy(keyboard_driver_t *drv);

#endif /* KEYBOARD_DRIVER_H */
