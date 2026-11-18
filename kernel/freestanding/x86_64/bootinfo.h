#ifndef FL_FREESTANDING_BOOTINFO_H
#define FL_FREESTANDING_BOOTINFO_H
#include "bootmap.h"
/* Boot-info handoff from stage 2, layout shared with bootinfo.inc. */

#include <stdint.h>

#define FL_BOOTINFO_MAGIC 0x49424c46u
#define FL_BOOTINFO_VERSION 1u
#define FL_BOOTINFO_SIZE 200u
#define FL_BOOTINFO_ADDR 0x510u
#define FL_BOOTINFO_E820_ADDR 0x600u
#define FL_BOOTINFO_E820_CAP 106u
#define FL_BOOTINFO_MAX_RESERVED 8u
#define FL_BOOTINFO_F_E820_TRUNCATED 1u
#define FL_PAGE_TABLES 0x1000u
#define FL_PAGE_TABLES_END 0x5000u
#define FL_KERNEL_STACK_BOTTOM 0xa000u
#define FL_KERNEL_STACK_TOP 0x10000u
#define FL_KERNEL_BSS_ADDR 0x100000u

typedef struct fl_boot_info {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t flags;
    uint32_t boot_drive;
    uint64_t e820_addr;
    uint32_t e820_count;
    uint32_t e820_capacity;
    uint64_t kernel_base;
    uint64_t kernel_load_end;
    uint64_t kernel_end;
    uint64_t mapped_end;
    uint32_t reserved_count;
    uint32_t pad;
    fl_boot_range_t reserved[FL_BOOTINFO_MAX_RESERVED];
} fl_boot_info_t;

_Static_assert(sizeof(fl_boot_info_t) == FL_BOOTINFO_SIZE, "boot info size is shared with bootinfo.inc");
_Static_assert(FL_BOOTINFO_ADDR + FL_BOOTINFO_SIZE <= FL_BOOTINFO_E820_ADDR, "boot info overlaps the E820 buffer");
_Static_assert(FL_BOOTINFO_E820_ADDR + FL_BOOTINFO_E820_CAP * 24u <= FL_PAGE_TABLES, "E820 buffer overlaps the page tables");

/* Returns OK, E_INVAL, or E_TRUNCATED for a truncated E820 map. */
/* image_end: the end of the loaded bytes; [bss_start, kernel_end) is the bss above 1 MiB. */
int fl_bootinfo_validate(const fl_boot_info_t *bi, uint64_t kernel_start, uint64_t image_end, uint64_t bss_start,
                         uint64_t kernel_end);

#endif
