/**
 * Bare-metal block transport layer.
 *
 * Provides the fl_hal_block_transport_t implementation for real hardware:
 *   x86_64 — ATA PIO (primary channel, master drive, LBA28).
 *            ata_pio_read_sector / ata_pio_write_sector in ata_pio.s;
 *            sector count from IDENTIFY (ata_pio_baremetal.c).
 *   AArch64        — RAM disk in .bss (4 MB, 8192 × 512-byte sectors).
 *                   ramdisk_read / ramdisk_write in
 *                   kernel/arch/aarch64/drivers/ramdisk.s.
 *
 * The host transport (block_transport_host.c) is compiled in host mode
 * only and is not linked into bare-metal builds.
 *
 * Disk geometry uses 512-byte sectors; x86 bare-metal sector count
 * comes from ATA IDENTIFY DEVICE (see ata_pio_baremetal.c).
 */
#ifdef DRIVERS_BAREMETAL

#include "block_driver.h"
#include "fl/driver/block.h"
#include "fl/driver/driver_types.h"

#if defined(__x86_64__)
#include "drivers/ata_pio.h"

static uint32_t s_bm_x86_sectors;

static int bm_read(void *hal_ctx, uint32_t lba, void *buf) {
    (void)hal_ctx;
    return ata_pio_read_sector(lba, buf) == 0 ? 0 : -1;
}
static int bm_write(void *hal_ctx, uint32_t lba, const void *buf) {
    (void)hal_ctx;
    return ata_pio_write_sector(lba, buf) == 0 ? 0 : -1;
}
static int bm_sector_count(void *hal_ctx) {
    (void)hal_ctx;
    return (int)s_bm_x86_sectors;
}

#elif defined(__aarch64__)
#include "drivers/ramdisk.h"

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
#endif

/* Forward declaration — fl_block_driver_create is in block_driver.c */
fl_block_driver_t *fl_block_driver_create(const fl_hal_block_transport_t *transport);

#if defined(__x86_64__)
block_driver_t *block_driver_create_baremetal(void) {
    uint32_t n = 0;
    if (ata_pio_probe_disk(&n) != 0 || n == 0u)
        return NULL;
    s_bm_x86_sectors = n;
    fl_hal_block_transport_t t = {
        .read             = bm_read,
        .write            = bm_write,
        .get_sector_count = bm_sector_count,
        .hal_ctx          = NULL,
    };
    return (block_driver_t *)fl_block_driver_create(&t);
}

#elif defined(__aarch64__)
block_driver_t *block_driver_create_baremetal(void) {
    fl_hal_block_transport_t t = {
        .read             = bm_read,
        .write            = bm_write,
        .get_sector_count = bm_sector_count,
        .hal_ctx          = NULL,
    };
    return (block_driver_t *)fl_block_driver_create(&t);
}

#else
block_driver_t *block_driver_create_baremetal(void) {
    return NULL;
}
#endif

#endif /* DRIVERS_BAREMETAL */
