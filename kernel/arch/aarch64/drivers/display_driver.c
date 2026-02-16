/* AArch64 display driver - simple UART/console or framebuffer stub.
 * For host builds this maps to printf; for baremetal map to platform-specific frame buffer or UART.
 */
#include "display_driver.h"
#include "driver_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
    display_driver_t base;
    int row, col;
    uint8_t color;
} display_impl_t;

static void host_putchar(display_driver_t *drv, char c) {
    (void)drv;
    putchar(c);
}

static void host_clear(display_driver_t *drv) {
    (void)drv;
    printf("\033[2J\033[H");
}

static void host_set_cursor(display_driver_t *drv, int row, int col) {
    (void)drv;
    printf("\033[%d;%dH", row + 1, col + 1);
}

static void host_refresh_vga(display_driver_t *drv, const void *vga_buf) {
    (void)drv; (void)vga_buf;
    /* AArch64 port may not have VGA - print a placeholder */
}

display_driver_t *display_driver_create(void) {
    display_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->color = 0x07;
    impl->base.putchar = host_putchar;
    impl->base.clear = host_clear;
    impl->base.set_cursor = host_set_cursor;
    impl->base.refresh_vga = host_refresh_vga;
    impl->base.impl = impl;
    return &impl->base;
}

void display_driver_destroy(display_driver_t *drv) {
    free(drv);
}
