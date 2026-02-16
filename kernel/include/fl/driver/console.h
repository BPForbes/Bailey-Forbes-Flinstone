/**
 * Console interface - display + keyboard.
 * x86: VGA/serial. ARM: PL011 UART. VM: SDL or virtual serial.
 */
#ifndef FL_DRIVER_CONSOLE_H
#define FL_DRIVER_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_VGA_COLS  80
#define FL_VGA_ROWS  25
#define FL_VGA_BASE  0xB8000

/* Display */
typedef struct fl_display_driver fl_display_driver_t;
struct fl_display_driver {
    void (*putchar)(fl_display_driver_t *drv, char c);
    void (*clear)(fl_display_driver_t *drv);
    void (*set_cursor)(fl_display_driver_t *drv, int row, int col);
    void (*refresh_vga)(fl_display_driver_t *drv, const void *vga_buf);  /* VM: copy to window */
    void *impl;
};

/* Keyboard */
typedef struct fl_keyboard_driver fl_keyboard_driver_t;
struct fl_keyboard_driver {
    int (*poll_scancode)(fl_keyboard_driver_t *drv, uint8_t *out);
    int (*get_char)(fl_keyboard_driver_t *drv, char *out);
    void *impl;
};

/* Timer */
typedef struct fl_timer_driver fl_timer_driver_t;
struct fl_timer_driver {
    uint64_t (*tick_count)(fl_timer_driver_t *drv);
    void (*msleep)(fl_timer_driver_t *drv, unsigned int ms);
    void *impl;
};

/* IRQ controller (PIC/GIC) */
typedef struct fl_pic_driver fl_pic_driver_t;
struct fl_pic_driver {
    void (*init)(fl_pic_driver_t *drv);
    void (*eoi)(fl_pic_driver_t *drv, int irq);
    void *impl;
};

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_CONSOLE_H */
