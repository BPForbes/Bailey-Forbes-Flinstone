#ifndef BLOCK_DRIVER_H
#define BLOCK_DRIVER_H

#include "driver_types.h"

/* Create block driver - wraps disk_asm / host file.
 * Host mode: uses fopen/fread/fwrite on disk file.
 * BAREMETAL mode: would use IDE port I/O (future). */
block_driver_t *block_driver_create_host(const char *disk_file);
void block_driver_destroy(block_driver_t *drv);

#endif /* BLOCK_DRIVER_H */
