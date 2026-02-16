/* AArch64 keyboard driver stub: host uses stdin, baremetal must implement platform input */
#include "keyboard_driver.h"
#include "driver_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    keyboard_driver_t base;
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

keyboard_driver_t *keyboard_driver_create(void) {
    keyboard_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->base.poll_scancode = host_poll_scancode;
    impl->base.get_char = host_get_char;
    impl->base.impl = impl;
    return &impl->base;
}

void keyboard_driver_destroy(keyboard_driver_t *drv) {
    free(drv);
}
