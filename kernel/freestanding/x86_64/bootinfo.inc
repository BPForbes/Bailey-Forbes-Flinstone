; Shared with bootinfo.h (tests/test_fs_bootmap.c checks the match); boot-info fields are little-endian.
; The A20 probe byte (0x5f0) may keep 0xa5; its alias 0x1005f0 is in the bss, which entry.asm zeroes after it. The kernel stack has no guard page.
%define FL_BOOTINFO_MAGIC            0x49424c46 ; "FLBI"
%define FL_BOOTINFO_VERSION          1
%define FL_BOOTINFO_SIZE             200
%define FL_BOOTINFO_ADDR             0x510
%define FL_BOOTINFO_E820_ADDR        0x600
%define FL_BOOTINFO_E820_CAP         106 ; all of [0x600, 0x1000)
%define FL_BOOTINFO_MAX_RESERVED     8
%define FL_BOOTINFO_F_E820_TRUNCATED 1
%define FL_A20_PROBE                 0x5f0
%define FL_PAGE_TABLES               0x1000
%define FL_PAGE_TABLES_END           0x5000
%define FL_LOADER_STACK_BOTTOM       0x7000
%define FL_STAGE1_ADDR               0x7c00
%define FL_STAGE2_ADDR               0x7e00
%define FL_STAGE2_SECTORS            8
%define FL_STAGE2_MAGIC              0x32534c46 ; "FLS2", the last 4 bytes of stage 2
%define FL_KERNEL_STACK_BOTTOM       0xa000 ; 24 KiB: the KeyQuorum history checkpoints and Rust verify run on it
%define FL_KERNEL_STACK_TOP          0x10000
%define FL_KERNEL_LOAD_ADDR          0x10000
%define FL_LOW_BUDGET                0x9f000
%define FL_KERNEL_BSS_ADDR           0x100000 ; the bss is never loaded: it lives above 1 MiB (linker.ld)
%define FL_IDENTITY_MAP_END          0x40000000

%define BI_MAGIC           0
%define BI_VERSION         4
%define BI_SIZE            6
%define BI_FLAGS           8
%define BI_BOOT_DRIVE      12
%define BI_E820_ADDR       16
%define BI_E820_COUNT      24
%define BI_E820_CAPACITY   28
%define BI_KERNEL_BASE     32
%define BI_KERNEL_LOAD_END 40
%define BI_KERNEL_END      48
%define BI_MAPPED_END      56
%define BI_RESERVED_COUNT  64
%define BI_RESERVED        72

%if FL_BOOTINFO_ADDR + FL_BOOTINFO_SIZE > FL_A20_PROBE || FL_A20_PROBE >= FL_BOOTINFO_E820_ADDR
%error "the A20 probe byte must lie between the boot-info struct and the E820 buffer"
%endif
%if FL_BOOTINFO_E820_ADDR + FL_BOOTINFO_E820_CAP * 24 > FL_PAGE_TABLES
%error "the E820 buffer overlaps the page tables"
%endif
%if FL_PAGE_TABLES_END > FL_LOADER_STACK_BOTTOM || FL_LOADER_STACK_BOTTOM >= FL_STAGE1_ADDR
%error "the page tables overlap the loader stack"
%endif
%if FL_STAGE1_ADDR + 512 != FL_STAGE2_ADDR
%error "stage 2 must follow stage 1"
%endif
%if FL_STAGE2_ADDR + FL_STAGE2_SECTORS * 512 > FL_KERNEL_STACK_BOTTOM
%error "stage 2 overlaps the kernel stack"
%endif
%if FL_KERNEL_BSS_ADDR < 0x100000 || FL_KERNEL_BSS_ADDR >= FL_IDENTITY_MAP_END
%error "the bss must start at or above 1 MiB, inside the identity map"
%endif
%if FL_KERNEL_STACK_TOP > FL_KERNEL_LOAD_ADDR || FL_KERNEL_LOAD_ADDR >= FL_LOW_BUDGET
%error "the kernel stack overlaps the image, or the image starts past the budget"
%endif
