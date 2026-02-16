/**
 * Canonical driver capability structs.
 * FL_CAP_REAL = hardware path works; FL_CAP_STUB = placeholder, may return 0xFF.
 */
#ifndef FL_DRIVER_CAPS_H
#define FL_DRIVER_CAPS_H

#include <stdint.h>

/* Real vs stub: kernel can refuse to use stubs for real work */
#define FL_CAP_REAL  0x80   /* driver has real hardware implementation */
#define FL_CAP_STUB  0x40   /* driver is placeholder (returns 0xFF, no-ops) */

typedef enum {
    FL_CAP_READ  = 1,
    FL_CAP_WRITE = 2,
    FL_CAP_POLL  = 4,
} fl_cap_flags_t;

/** Returns non-zero (FL_CAP_REAL) if driver is real; 0 if stub. */
static inline int fl_cap_is_real(uint32_t flags) {
    return (flags & FL_CAP_REAL) != 0;
}
static inline int fl_cap_is_stub(uint32_t flags) {
    return (flags & FL_CAP_STUB) != 0;
}

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
