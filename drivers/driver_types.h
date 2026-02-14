#ifndef DRIVER_TYPES_H
#define DRIVER_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define SECTOR_SIZE  512
#define VGA_COLS     80
#define VGA_ROWS     25
#define VGA_BASE     0xB8000

/**
 * Driver interfaces: minimal, capability-based.
 * Each driver exposes only: init/shutdown + minimal read/write/irq hooks.
 * No driver-specific knowledge leaks into FS/shell layers.
 */

/* Block device - sector read/write */
struct block_caps;  /* see driver_caps.h */
typedef struct block_driver block_driver_t;
struct block_driver {
    int (*read_sector)(block_driver_t *drv, uint32_t lba, void *buf);
    int (*write_sector)(block_driver_t *drv, uint32_t lba, const void *buf);
    int (*get_caps)(block_driver_t *drv, struct block_caps *out);
    uint32_t sector_count;
    void *impl;
};

/* Keyboard - scancode/char input */
typedef struct keyboard_driver keyboard_driver_t;
struct keyboard_driver {
    int (*poll_scancode)(keyboard_driver_t *drv, uint8_t *out);
    int (*get_char)(keyboard_driver_t *drv, char *out);
    void *impl;
};

/* Display - console output */
typedef struct display_driver display_driver_t;
struct display_driver {
    void (*putchar)(display_driver_t *drv, char c);
    void (*clear)(display_driver_t *drv);
    void (*set_cursor)(display_driver_t *drv, int row, int col);
    /* VM: refresh from raw VGA buffer (80*25*2 bytes, char+attr per cell) */
    void (*refresh_vga)(display_driver_t *drv, const void *vga_buf);
    void *impl;
};

/* Timer - ticks and sleep */
typedef struct timer_driver timer_driver_t;
struct timer_driver {
    uint64_t (*tick_count)(timer_driver_t *drv);
    void (*msleep)(timer_driver_t *drv, unsigned int ms);
    void *impl;
};

/* PIC - interrupt controller */
typedef struct pic_driver pic_driver_t;
struct pic_driver {
    void (*init)(pic_driver_t *drv);
    void (*eoi)(pic_driver_t *drv, int irq);
    void *impl;
};

#endif /* DRIVER_TYPES_H */
