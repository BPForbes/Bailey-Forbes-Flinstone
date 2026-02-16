/**
 * Canonical driver capability structs.
 */
#ifndef FL_DRIVER_CAPS_H
#define FL_DRIVER_CAPS_H

#include <stdint.h>

typedef enum {
    FL_CAP_READ = 1,
    FL_CAP_WRITE = 2,
    FL_CAP_POLL = 4,
} fl_cap_flags_t;

/* Block caps in block.h / driver_types.h */

typedef struct {
    fl_cap_flags_t flags;
} fl_keyboard_caps_t;

typedef struct {
    int cols;
    int rows;
    fl_cap_flags_t flags;
} fl_display_caps_t;

typedef struct {
    fl_cap_flags_t flags;
} fl_timer_caps_t;

typedef struct {
    int max_irq;
    fl_cap_flags_t flags;
} fl_pic_caps_t;

#define cap_flags_t      fl_cap_flags_t
#define keyboard_caps_t  fl_keyboard_caps_t
#define display_caps_t  fl_display_caps_t
#define timer_caps_t    fl_timer_caps_t
#define pic_caps_t      fl_pic_caps_t

#endif /* FL_DRIVER_CAPS_H */
