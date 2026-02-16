/* Keyboard driver - input from user.
 * Host: read() from stdin. BAREMETAL: port_inb(0x60). */
#include "keyboard_driver.h"
#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef DRIVERS_BAREMETAL
#define KB_DATA   0x60
#define KB_STATUS 0x64
#endif

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
    if ((port_inb(KB_STATUS) & 0x01) == 0)
        return -1;
    *out = (uint8_t)port_inb(KB_DATA);
    return 0;
}

static int hw_get_char(keyboard_driver_t *drv, char *out) {
    uint8_t sc;
    if (hw_poll_scancode(drv, &sc) != 0)
        return -1;
    /* Simplified: scancode set 1, no modifier. Full impl needs translation table. */
    *out = (sc < 128) ? (char)sc : '\0';
    return 0;
}
#endif

keyboard_driver_t *keyboard_driver_create(void) {
    keyboard_impl_t *impl = calloc(1, sizeof(*impl));
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
