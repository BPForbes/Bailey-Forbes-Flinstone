#ifndef FL_BLOCK_DRIVER_H
#define FL_BLOCK_DRIVER_H

#include "fl/driver/driver_types.h"

block_driver_t *block_driver_create_host(const char *disk_file);
void block_driver_destroy(block_driver_t *drv);

#endif
