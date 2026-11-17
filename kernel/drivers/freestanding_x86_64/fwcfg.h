#ifndef FL_FREESTANDING_FWCFG_H
#define FL_FREESTANDING_FWCFG_H

#include <stddef.h>
#include <stdint.h>

int fl_fs_fwcfg_read(const char *name, uint32_t offset, uint8_t *out, size_t len);

#endif
