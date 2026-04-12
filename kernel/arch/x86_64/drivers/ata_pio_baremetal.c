/**
 * ATA PIO bare-metal helpers: IDENTIFY DEVICE for real sector geometry.
 * Uses fl_ioport (same ports as ata_pio.s). Only for x86_64 + DRIVERS_BAREMETAL.
 */
#include <stdint.h>

#if defined(DRIVERS_BAREMETAL) && defined(__x86_64__)

#include "ata_pio.h"
#include "fl/driver/ioport.h"

#define ATA_DATA     0x1F0u
#define ATA_SECCOUNT 0x1F2u
#define ATA_LBA_LO   0x1F3u
#define ATA_LBA_MID  0x1F4u
#define ATA_LBA_HI   0x1F5u
#define ATA_DRIVE    0x1F6u
#define ATA_STATCMD  0x1F7u

#define ATA_CMD_IDENTIFY 0xECu

#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_DF  0x20u
#define ATA_SR_BSY 0x80u

#define ATA_ID_TIMEOUT 8000000u

static int ata_wait_not_bsy(uint32_t max_iter) {
    for (uint32_t i = 0; i < max_iter; i++) {
        uint8_t st = fl_ioport_in8(ATA_STATCMD);
        if (st & (ATA_SR_ERR | ATA_SR_DF))
            return -1;
        if ((st & ATA_SR_BSY) == 0)
            return 0;
    }
    return -1;
}

int ata_pio_probe_disk(uint32_t *sector_count_out) {
    if (!sector_count_out)
        return -1;
    *sector_count_out = 0;

    if (ata_wait_not_bsy(ATA_ID_TIMEOUT) != 0)
        return -1;

    fl_ioport_out8(ATA_DRIVE, 0xA0u);
    fl_ioport_out8(ATA_SECCOUNT, 0);
    fl_ioport_out8(ATA_LBA_LO, 0);
    fl_ioport_out8(ATA_LBA_MID, 0);
    fl_ioport_out8(ATA_LBA_HI, 0);
    fl_ioport_out8(ATA_STATCMD, ATA_CMD_IDENTIFY);

    if (ata_wait_not_bsy(ATA_ID_TIMEOUT) != 0)
        return -1;

    uint8_t st = fl_ioport_in8(ATA_STATCMD);
    if (st & (ATA_SR_ERR | ATA_SR_DF))
        return -1;
    if ((st & ATA_SR_DRQ) == 0)
        return -1;

    uint16_t id[256];
    for (int w = 0; w < 256; w++) {
        id[w] = fl_ioport_in16(ATA_DATA);
    }

    /* Words 60–61: total user-addressable sectors (28-bit LBA). */
    uint32_t n = ((uint32_t)id[61] << 16) | (uint32_t)id[60];
    if (n == 0u)
        return -1;
    /* LBA28 PIO path supports 28-bit LBAs only. */
    if (n > 0x0FFFFFFFu)
        n = 0x0FFFFFFFu;
    *sector_count_out = n;
    return 0;
}

#else /* !DRIVERS_BAREMETAL || !__x86_64__ */

/* Host / non-x86 builds link this TU; keep a defined symbol for optional callers. */
int ata_pio_probe_disk(uint32_t *sector_count_out) {
    if (sector_count_out)
        *sector_count_out = 0u;
    return -1;
}

#endif

