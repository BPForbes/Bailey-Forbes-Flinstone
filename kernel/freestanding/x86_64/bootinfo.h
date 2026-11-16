#ifndef FL_FREESTANDING_BOOTINFO_H
#define FL_FREESTANDING_BOOTINFO_H
/*
 * Boot-info handoff: stage 2 (stage2.asm) fills this struct in low memory, outside the
 * kernel's bss, and enters the kernel with its address in rdi. fl_kernel_main validates it with
 * fl_bootinfo_validate before anything else starts. The layout is shared with bootinfo.inc,
 * which tests/test_fs_bootmap.c checks against this header.
 */
#include "bootmap.h"

#include <stdint.h>

#define FL_BOOTINFO_MAGIC 0x49424c46u /* "FLBI" */
#define FL_BOOTINFO_VERSION 1u
#define FL_BOOTINFO_SIZE 200u
#define FL_BOOTINFO_ADDR 0x510u
#define FL_BOOTINFO_E820_ADDR 0x600u
#define FL_BOOTINFO_E820_CAP 106u
#define FL_BOOTINFO_MAX_RESERVED 8u
#define FL_BOOTINFO_F_E820_TRUNCATED 1u /* the BIOS had more entries than the buffer holds */
#define FL_PAGE_TABLES 0x1000u /* the loader's identity map, live for the whole run */
#define FL_PAGE_TABLES_END 0x5000u
#define FL_KERNEL_STACK_BOTTOM 0xc000u /* entry.asm's stack, below the image; no guard page */
#define FL_KERNEL_STACK_TOP 0x10000u

typedef struct fl_boot_info {
    uint32_t magic;
    uint16_t version;
    uint16_t size;            /* sizeof(fl_boot_info_t) */
    uint32_t flags;           /* FL_BOOTINFO_F_* */
    uint32_t boot_drive;      /* the BIOS drive number stage 1 was started with */
    uint64_t e820_addr;       /* fl_e820_entry_t[e820_capacity] */
    uint32_t e820_count;
    uint32_t e820_capacity;
    uint64_t kernel_base;     /* where stage 2 loaded the image (the link address) */
    uint64_t kernel_load_end; /* end of the whole sectors it read */
    uint64_t kernel_end;      /* end of the image's bss */
    uint64_t mapped_end;      /* [0, mapped_end) is identity-mapped */
    uint32_t reserved_count;
    uint32_t pad;
    fl_boot_range_t reserved[FL_BOOTINFO_MAX_RESERVED]; /* memory the loader and the image occupy */
} fl_boot_info_t;

_Static_assert(sizeof(fl_boot_info_t) == FL_BOOTINFO_SIZE, "boot info size is shared with bootinfo.inc");
_Static_assert(FL_BOOTINFO_ADDR + FL_BOOTINFO_SIZE <= FL_BOOTINFO_E820_ADDR, "boot info overlaps the E820 buffer");
_Static_assert(FL_BOOTINFO_E820_ADDR + FL_BOOTINFO_E820_CAP * 24u <= FL_PAGE_TABLES, "E820 buffer overlaps the page tables");

/* FL_BOOTMAP_OK, or the reason the handoff is unusable (FL_BOOTMAP_E_INVAL, or
 * FL_BOOTMAP_E_TRUNCATED for a truncated E820 map). kernel_start, bss_start and kernel_end are the
 * image's own linker symbols: the loader must agree with them. */
int fl_bootinfo_validate(const fl_boot_info_t *bi, uint64_t kernel_start, uint64_t bss_start, uint64_t kernel_end);

#endif
