#ifndef FL_BLOCK_DRIVER_H
#define FL_BLOCK_DRIVER_H

#include "fl/driver/driver_types.h"

/* Host mode: file-backed text-format disk */
block_driver_t *block_driver_create_host(const char *disk_file);

/* Bare-metal: ATA PIO (x86_64) or RAM disk (AArch64) */
block_driver_t *block_driver_create_baremetal(void);

void block_driver_destroy(block_driver_t *drv);

#endif
