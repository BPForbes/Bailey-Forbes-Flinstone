#ifndef X86_ATA_PIO_H
#define X86_ATA_PIO_H

#include <stdint.h>

/**
 * ATA PIO 28-bit LBA sector I/O (primary channel, master drive).
 * Each call transfers exactly 512 bytes.
 * Implemented in ata_pio.s using rep insw / rep outsw.
 * Returns 0 on success, -1 on timeout or ATA error (ERR/DF).
 */
int ata_pio_read_sector (uint32_t lba, void *buf);
int ata_pio_write_sector(uint32_t lba, const void *buf);

/**
 * Issue IDENTIFY DEVICE and return total LBA28 sector count in *sector_count_out.
 * Returns 0 on success, -1 if the drive does not respond or geometry is unknown.
 * Defined in ata_pio_baremetal.c (host builds provide a stub).
 */
int ata_pio_probe_disk(uint32_t *sector_count_out);

#endif /* X86_ATA_PIO_H */
