/**
 * Unified display driver - uses fl_mmio for baremetal VGA.
 * Host: printf. BAREMETAL: VGA at 0xB8000 via MMIO.
 */
#include "fl/driver/console.h"
#include "fl/driver/mmio.h"
#include "fl/driver/driver_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef DRIVERS_BAREMETAL
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

static void host_refresh_vga(display_driver_t *drv, const void *vga_buf) {
    (void)drv;
    const uint16_t *cells = (const uint16_t *)vga_buf;
    printf("\033[2J\033[H");
    for (int r = 0; r < VGA_ROWS; r++) {
        for (int c = 0; c < VGA_COLS; c++) {
            uint16_t word = cells[r * VGA_COLS + c];
            char ch = (char)(word & 0xFF);
            if (ch < 32 && ch != '\n') ch = ' ';
            putchar(ch);
        }
        if (r < VGA_ROWS - 1) putchar('\n');
    }
    fflush(stdout);
}

#ifdef DRIVERS_BAREMETAL
static void hw_putchar(display_driver_t *drv, char c) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    if (c == '\n') {
        impl->col = 0;
        if (++impl->row >= VGA_ROWS) {
            impl->row = VGA_ROWS - 1;
            for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
                fl_mmio_write16((void *)(VGA_MEM + i), fl_mmio_read16((void *)(VGA_MEM + i + VGA_COLS)));
            for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
                fl_mmio_write16((void *)(VGA_MEM + i), (uint16_t)(' ' | (impl->color << 8)));
        }
        return;
    }
    int idx = impl->row * VGA_COLS + impl->col;
    fl_mmio_write16((void *)(VGA_MEM + idx), (uint16_t)(uint8_t)c | (impl->color << 8));
    if (++impl->col >= VGA_COLS) {
        impl->col = 0;
        if (++impl->row >= VGA_ROWS) {
            impl->row = VGA_ROWS - 1;
            for (int i = 0; i < (VGA_ROWS - 1) * VGA_COLS; i++)
                fl_mmio_write16((void *)(VGA_MEM + i), fl_mmio_read16((void *)(VGA_MEM + i + VGA_COLS)));
            for (int i = (VGA_ROWS - 1) * VGA_COLS; i < VGA_ROWS * VGA_COLS; i++)
                fl_mmio_write16((void *)(VGA_MEM + i), (uint16_t)(' ' | (impl->color << 8)));
        }
    }
}

static void hw_clear(display_driver_t *drv) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    uint16_t blank = (uint16_t)(' ' | (impl->color << 8));
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        fl_mmio_write16((void *)(VGA_MEM + i), blank);
    impl->row = impl->col = 0;
}

static void hw_set_cursor(display_driver_t *drv, int row, int col) {
    display_impl_t *impl = (display_impl_t *)drv->impl;
    impl->row = row;
    impl->col = col;
}

static void hw_refresh_vga(display_driver_t *drv, const void *vga_buf) {
    (void)drv;
    const uint16_t *cells = (const uint16_t *)vga_buf;
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        fl_mmio_write16((void *)(VGA_MEM + i), cells[i]);
}
#endif

display_driver_t *display_driver_create(void) {
    display_impl_t *impl = (display_impl_t *)calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->color = 0x07;
#ifdef DRIVERS_BAREMETAL
    impl->base.putchar = hw_putchar;
    impl->base.clear = hw_clear;
    impl->base.set_cursor = hw_set_cursor;
    impl->base.refresh_vga = hw_refresh_vga;
    impl->vga = VGA_MEM;
#else
    impl->base.putchar = host_putchar;
    impl->base.clear = host_clear;
    impl->base.set_cursor = host_set_cursor;
    impl->base.refresh_vga = host_refresh_vga;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void display_driver_destroy(display_driver_t *drv) {
    free(drv);
}
