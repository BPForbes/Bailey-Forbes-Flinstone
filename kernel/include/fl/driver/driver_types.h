/**
 * Canonical driver types - single definition for all platforms.
 * Replaces arch-specific and VM driver_types.h. All include this.
 */
#ifndef FL_DRIVER_TYPES_H
#define FL_DRIVER_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "block.h"
#include "console.h"
#include "net.h"

#define SECTOR_SIZE  FL_SECTOR_SIZE
#define VGA_COLS     FL_VGA_COLS
#define VGA_ROWS     FL_VGA_ROWS
#define VGA_BASE     FL_VGA_BASE

/* Legacy type aliases - allow gradual migration from block_driver_t to fl_block_driver_t */
typedef fl_block_driver_t     block_driver_t;
typedef fl_block_caps_t      block_caps_t;
typedef fl_display_driver_t   display_driver_t;
typedef fl_keyboard_driver_t  keyboard_driver_t;
typedef fl_timer_driver_t     timer_driver_t;
typedef fl_pic_driver_t       pic_driver_t;

#define CAP_READ   FL_BLOCK_CAP_READ
#define CAP_WRITE  FL_BLOCK_CAP_WRITE
#define CAP_POLL   4

/* Legacy block_caps struct alias */
struct block_caps {
    uint32_t max_sector;
    uint32_t sector_size;
    uint32_t max_transfer;
    uint32_t flags;
};

#endif /* FL_DRIVER_TYPES_H */
