#ifndef FL_FREESTANDING_BOOTMAP_H
#define FL_FREESTANDING_BOOTMAP_H
/* Boot memory map: picks the heap arena from the firmware's E820 map. Pure C, no hardware access. */
#include <stdint.h>

#define FL_E820_USABLE 1u
#define FL_E820_ATTR_ENABLED 1u /* ACPI 3.0 extended attribute bit 0 */

typedef struct fl_e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} fl_e820_entry_t; /* the BIOS's 24-byte INT 15h E820 record */

typedef struct fl_boot_range {
    uint64_t base;
    uint64_t end; /* exclusive */
} fl_boot_range_t;

typedef struct fl_bootmap_limits {
    uint64_t min_base; /* lowest address the arena may use */
    uint64_t max_end;  /* the arena ends at or below this (e.g. the end of the identity map) */
    uint64_t min_size; /* smaller than this: no arena */
    uint64_t align;    /* power of two, at least 1 */
} fl_bootmap_limits_t;

enum {
    FL_BOOTMAP_OK = 0,
    FL_BOOTMAP_E_INVAL = -1,     /* null pointer, bad limits, or an empty or inverted reservation */
    FL_BOOTMAP_E_TRUNCATED = -2, /* no entries, or more than the buffer holds */
    FL_BOOTMAP_E_OVERFLOW = -3,  /* an entry's base + length passes 2^64 */
    FL_BOOTMAP_E_NOFIT = -4      /* no span of min_size */
};

/*
 * Selects the largest span (lowest on a tie) of usable memory in [min_base, max_end), aligned and
 * clear of every non-usable entry and reservation. Zero-length and disabled entries are ignored.
 * Returns FL_BOOTMAP_OK or an FL_BOOTMAP_E_* code; any error returns no arena.
 */
int fl_bootmap_select_arena(const fl_e820_entry_t *entries, uint32_t count, uint32_t capacity,
                            const fl_boot_range_t *reservations, uint32_t reservation_count,
                            const fl_bootmap_limits_t *limits, uint64_t *out_base, uint64_t *out_size);

#endif
