/**
 * Driver capability structs - what each driver supports, limits.
 * Keeps driver knowledge out of FS/shell layers.
 */
#ifndef DRIVER_CAPS_H
#define DRIVER_CAPS_H

#include <stdint.h>

typedef enum {
    CAP_READ = 1,
    CAP_WRITE = 2,
    CAP_POLL = 4,
} cap_flags_t;

/* Block device capabilities (forward decl in driver_types) */
struct block_caps {
    uint32_t max_sector;
    uint32_t sector_size;
    uint32_t max_transfer;
    cap_flags_t flags;
};
typedef struct block_caps block_caps_t;

/* Keyboard capabilities */
typedef struct keyboard_caps {
    cap_flags_t flags;        /* CAP_POLL */
} keyboard_caps_t;

/* Display capabilities */
typedef struct display_caps {
    int cols;
    int rows;
    cap_flags_t flags;
} display_caps_t;

/* Timer capabilities */
typedef struct timer_caps {
    cap_flags_t flags;
} timer_caps_t;

/* PIC capabilities */
typedef struct pic_caps {
    int max_irq;
    cap_flags_t flags;
} pic_caps_t;

#endif /* DRIVER_CAPS_H */
