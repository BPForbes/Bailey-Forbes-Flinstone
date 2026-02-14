#ifndef VM_LOADER_H
#define VM_LOADER_H

#include "vm_mem.h"
#include <stddef.h>

#define GUEST_LOAD_ADDR  0x7c00

int vm_load_binary(vm_mem_t *mem, uint32_t addr, const void *data, size_t len);
int vm_load_file(vm_mem_t *mem, uint32_t addr, const char *path);

#endif /* VM_LOADER_H */
