/**
 * Bare-metal block transport layer.
 *
 * Provides the fl_hal_block_transport_t implementation for real hardware:
 *   x86_64 / i386 — ATA PIO (primary channel, master drive, LBA28).
 *                   ata_pio_read_sector / ata_pio_write_sector in
 *                   kernel/arch/x86_64/drivers/ata_pio.s.
 *   AArch64        — RAM disk in .bss (4 MB, 8192 × 512-byte sectors).
 *                   ramdisk_read / ramdisk_write in
 *                   kernel/arch/aarch64/drivers/ramdisk.s.
 *
 * The host transport (block_transport_host.c) is compiled in host mode
 * only and is not linked into bare-metal builds.
 *
 * Disk geometry is fixed at SECTOR_SIZE (512) bytes per sector;
 * the sector count is a compile-time constant that can be refined
 * once ATA IDENTIFY DEVICE support is added.
 */
#ifdef DRIVERS_BAREMETAL

#include "block_driver.h"
#include "fl/driver/block.h"
#include "fl/driver/driver_types.h"

#if defined(__x86_64__) || defined(__i386__)
#include "ata_pio.h"
/* Conservative upper bound; refine with ATA IDENTIFY DEVICE later */
#define BM_SECTOR_COUNT 65536u  /* 32 MB */

static int bm_read(void *hal_ctx, uint32_t lba, void *buf) {
    (void)hal_ctx;
    ata_pio_read_sector(lba, buf);
    return 0;
}
static int bm_write(void *hal_ctx, uint32_t lba, const void *buf) {
    (void)hal_ctx;
    ata_pio_write_sector(lba, buf);
    return 0;
}
static int bm_sector_count(void *hal_ctx) {
    (void)hal_ctx;
    return (int)BM_SECTOR_COUNT;
}

#elif defined(__aarch64__)
#include "ramdisk.h"

static int bm_read(void *hal_ctx, uint32_t lba, void *buf) {
    (void)hal_ctx;
    return ramdisk_read((int)lba, (unsigned char *)buf);
}
static int bm_write(void *hal_ctx, uint32_t lba, const void *buf) {
    (void)hal_ctx;
    return ramdisk_write((int)lba, (const unsigned char *)buf);
}
static int bm_sector_count(void *hal_ctx) {
    (void)hal_ctx;
    return RAMDISK_SECTORS;
}
#else
/* Unsupported bare-metal arch — no-op transport */
static int bm_read(void *h, uint32_t l, void *b)       { (void)h;(void)l;(void)b; return -1; }
static int bm_write(void *h, uint32_t l, const void *b){ (void)h;(void)l;(void)b; return -1; }
static int bm_sector_count(void *h)                    { (void)h; return 0; }
#endif

/* Forward declaration — fl_block_driver_create is in block_driver.c */
fl_block_driver_t *fl_block_driver_create(const fl_hal_block_transport_t *transport);

block_driver_t *block_driver_create_baremetal(void) {
    fl_hal_block_transport_t t;
    t.read             = bm_read;
    t.write            = bm_write;
    t.get_sector_count = bm_sector_count;
    t.hal_ctx          = NULL;
    return (block_driver_t *)fl_block_driver_create(&t);
}

#endif /* DRIVERS_BAREMETAL */
