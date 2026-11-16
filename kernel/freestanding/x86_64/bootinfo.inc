; Boot layout and boot-info handoff (FL-02), shared by boot.asm (stage 1) and stage2.asm.
; Every value here must match bootinfo.h: tests/test_fs_bootmap.c reads this file and checks.
;
; Low memory while the loader runs:
;   0x00000-0x004ff  IVT and BIOS data area
;   0x00500-0x005ff  boot-info struct (FL_BOOTINFO_SIZE bytes; 0x5f0 is the A20 probe byte)
;   0x00600-0x00bff  E820 buffer (FL_BOOTINFO_E820_CAP 24-byte entries)
;   0x01000-0x04fff  page tables (PML4, PDPT, PD: the first 1 GiB in 2 MiB pages)
;   0x07000-0x07bff  loader stack
;   0x07c00-0x07dff  stage 1 (the MBR)
;   0x07e00-...      stage 2 (FL_STAGE2_SECTORS sectors)
;   0x0c000-0x0ffff  kernel stack (entry.asm), outside the image so its 16 KiB do not count
;                    against FL_LOW_BUDGET
;   0x10000-...      kernel image and bss, below FL_LOW_BUDGET
%define FL_BOOTINFO_MAGIC            0x49424c46 ; "FLBI"
%define FL_BOOTINFO_VERSION          1
%define FL_BOOTINFO_SIZE             200
%define FL_BOOTINFO_ADDR             0x500
%define FL_BOOTINFO_E820_ADDR        0x600
%define FL_BOOTINFO_E820_CAP         64
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
%define FL_KERNEL_STACK_BOTTOM       0xc000
%define FL_KERNEL_STACK_TOP          0x10000
%define FL_KERNEL_LOAD_ADDR          0x10000
%define FL_LOW_BUDGET                0x9f000
%define FL_IDENTITY_MAP_END          0x40000000

; Field offsets in the boot-info struct (all little-endian).
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
