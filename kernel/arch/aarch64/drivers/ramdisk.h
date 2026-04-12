#ifndef AARCH64_RAMDISK_H
#define AARCH64_RAMDISK_H

#include <stdint.h>

/**
 * AArch64 RAM disk — 4 MB in-memory block device for bare-metal builds.
 *
 * The storage region (g_ramdisk_storage) lives in .bss and is zeroed at
 * boot.  Content is lost on power cycle; this is the intended behaviour
 * until persistent storage (virtio-blk, SD, etc.) is added.
 *
 * Implemented in ramdisk.s; transfer loops are ASM-backed.
 */

#define RAMDISK_SECTOR_SIZE 512
#define RAMDISK_SECTORS     8192   /* 4 MB */

extern unsigned char g_ramdisk_storage[];

/* Returns 0 on success, -1 on out-of-range lba */
int ramdisk_read (int lba, unsigned char *buf);
int ramdisk_write(int lba, const unsigned char *buf);

#endif /* AARCH64_RAMDISK_H */
