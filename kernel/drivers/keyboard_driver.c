/**
 * Unified keyboard driver - uses fl_ioport for baremetal.
 * Host: stdin. BAREMETAL: PS/2 port I/O via HAL.
 */
#include "drivers.h"
#include "fl/driver/console.h"
#include "fl/driver/ioport.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include <stdlib.h>
#include <unistd.h>

#define KB_DATA   0x60
#define KB_STATUS 0x64

typedef struct {
    keyboard_driver_t base;
    int host_mode;
} keyboard_impl_t;

static int host_poll_scancode(keyboard_driver_t *drv, uint8_t *out) {
    (void)drv;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        *out = (uint8_t)c;
        return 0;
    }
    return -1;
}

static int host_get_char(keyboard_driver_t *drv, char *out) {
    uint8_t sc;
    if (host_poll_scancode(drv, &sc) == 0) {
        *out = (char)sc;
        return 0;
    }
    return -1;
}

#ifdef DRIVERS_BAREMETAL
static int hw_poll_scancode(keyboard_driver_t *drv, uint8_t *out) {
    (void)drv;
    if ((fl_ioport_in8(KB_STATUS) & 0x01) == 0)
        return -1;
    *out = (uint8_t)fl_ioport_in8(KB_DATA);
    return 0;
}

static int hw_get_char(keyboard_driver_t *drv, char *out) {
    uint8_t sc;
    if (hw_poll_scancode(drv, &sc) != 0)
        return -1;
    *out = (sc < 128) ? (char)sc : '\0';
    return 0;
}
#endif

keyboard_driver_t *keyboard_driver_create(void) {
    keyboard_impl_t *impl = (keyboard_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
    impl->host_mode = 0;
    impl->base.poll_scancode = hw_poll_scancode;
    impl->base.get_char = hw_get_char;
#else
    impl->host_mode = 1;
    impl->base.poll_scancode = host_poll_scancode;
    impl->base.get_char = host_get_char;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void keyboard_driver_destroy(keyboard_driver_t *drv) {
    free(drv);
}

uint32_t keyboard_driver_caps(void) {
    if (!g_keyboard_driver) return 0;
#ifndef DRIVERS_BAREMETAL
    return FL_CAP_REAL;
#elif defined(__x86_64__) || defined(__i386__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
}
