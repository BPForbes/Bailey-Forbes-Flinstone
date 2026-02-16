#ifndef VM_MEM_H
#define VM_MEM_H

#include <stddef.h>
#include <stdint.h>

#define GUEST_RAM_SIZE  (16 * 1024 * 1024)
#define GUEST_VGA_BASE  0xb8000
#define GUEST_VGA_SIZE  (80 * 25 * 2)

typedef struct vm_mem {
    uint8_t *ram;
    size_t size;
} vm_mem_t;

int vm_mem_init(vm_mem_t *mem);
void vm_mem_destroy(vm_mem_t *mem);
void vm_mem_zero(vm_mem_t *mem);
int vm_mem_load(vm_mem_t *mem, uint32_t guest_addr, const void *src, size_t n);
int vm_mem_read(vm_mem_t *mem, uint32_t guest_addr, void *dst, size_t n);
int vm_mem_write(vm_mem_t *mem, uint32_t guest_addr, const void *src, size_t n);
uint8_t vm_mem_read8(vm_mem_t *mem, uint32_t guest_addr);
uint16_t vm_mem_read16(vm_mem_t *mem, uint32_t guest_addr);
void vm_mem_write8(vm_mem_t *mem, uint32_t guest_addr, uint8_t v);
void vm_mem_write16(vm_mem_t *mem, uint32_t guest_addr, uint16_t v);

#endif /* VM_MEM_H */
