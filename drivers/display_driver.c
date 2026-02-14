/* Display driver - console output.
 * Host: printf. BAREMETAL: VGA text buffer at 0xB8000. */
#include "display_driver.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef DRIVERS_BAREMETAL
#include <stddef.h>
#define VGA_MEM ((volatile uint16_t *)VGA_BASE)
#endif

typedef struct {
    display_driver_t base;
    int row, col;
    uint8_t color;
#ifdef DRIVERS_BAREMETAL
    volatile uint16_t *vga;
#endif
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

#ifdef DRIVERS_BAREMETAL
static void hw_putchar(display_driver_t *drv, char c) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    if (c == '\n') {
        impl->col = 0;
        if (++impl->row >= VGA_ROWS) {
            impl->row = VGA_ROWS - 1;
            /* Scroll - shift rows up */
            for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
                VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
            for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
                VGA_MEM[i] = ' ' | (impl->color << 8);
        }
        return;
    }
    int idx = impl->row * VGA_COLS + impl->col;
    VGA_MEM[idx] = (uint16_t)(uint8_t)c | (impl->color << 8);
    if (++impl->col >= VGA_COLS) {
        impl->col = 0;
        if (++impl->row >= VGA_ROWS) {
            impl->row = VGA_ROWS - 1;
            for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
                VGA_MEM[i] = VGA_MEM[i + VGA_COLS];
            for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
                VGA_MEM[i] = ' ' | (impl->color << 8);
        }
    }
}

static void hw_clear(display_driver_t *drv) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    uint16_t blank = ' ' | (impl->color << 8);
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEM[i] = blank;
    impl->row = impl->col = 0;
}

static void hw_set_cursor(display_driver_t *drv, int row, int col) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    impl->row = row;
    impl->col = col;
}
#endif

display_driver_t *display_driver_create(void) {
    display_impl_t *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->color = 0x07;  /* light gray on black */
#ifdef DRIVERS_BAREMETAL
    impl->base.putchar = hw_putchar;
    impl->base.clear = hw_clear;
    impl->base.set_cursor = hw_set_cursor;
    impl->vga = VGA_MEM;
#else
    impl->base.putchar = host_putchar;
    impl->base.clear = host_clear;
    impl->base.set_cursor = host_set_cursor;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void display_driver_destroy(display_driver_t *drv) {
    free(drv);
}
