#ifndef FL_FREESTANDING_BOOTMAP_H
#define FL_FREESTANDING_BOOTMAP_H
#include <stdint.h>
/* Boot memory map: picks the heap arena from the E820 map. Pure C, no hardware access. */

#define FL_E820_USABLE 1u
#define FL_E820_ATTR_ENABLED 1u

typedef struct fl_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} fl_e820_entry_t;

typedef struct fl_boot_range {
    uint64_t base;
    uint64_t end;
} fl_boot_range_t;

typedef struct fl_bootmap_limits {
    uint64_t min_base;
    uint64_t max_end;
    uint64_t min_size;
    uint64_t align;
} fl_bootmap_limits_t;

enum {
    FL_BOOTMAP_OK = 0,
    FL_BOOTMAP_E_INVAL = -1,
    FL_BOOTMAP_E_TRUNCATED = -2,
    FL_BOOTMAP_E_OVERFLOW = -3,
    FL_BOOTMAP_E_NOFIT = -4
};

/* Any error rejects the whole map; no arena is returned. */
int fl_bootmap_select_arena(const fl_e820_entry_t *entries, uint32_t count, uint32_t capacity,
                            const fl_boot_range_t *reservations, uint32_t reservation_count,
                            const fl_bootmap_limits_t *limits, uint64_t *out_base, uint64_t *out_size);

#endif
