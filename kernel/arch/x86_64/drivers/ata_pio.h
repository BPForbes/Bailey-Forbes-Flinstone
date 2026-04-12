#ifndef X86_ATA_PIO_H
#define X86_ATA_PIO_H

#include <stdint.h>

/**
 * ATA PIO 28-bit LBA sector I/O (primary channel, master drive).
 * Each call transfers exactly 512 bytes.
 * Implemented in ata_pio.s using rep insw / rep outsw.
 * Only available when DRIVERS_BAREMETAL is defined.
 */
void ata_pio_read_sector (uint32_t lba, void *buf);
void ata_pio_write_sector(uint32_t lba, const void *buf);

#endif /* X86_ATA_PIO_H */
