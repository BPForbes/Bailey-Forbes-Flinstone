#include "vm_mem.h"
#include "mem_domain.h"
#include "mem_asm.h"
#include <stdlib.h>

int vm_mem_init(vm_mem_t *mem) {
    if (!mem) return -1;
    mem->ram = mem_domain_alloc(MEM_DOMAIN_USER, GUEST_RAM_SIZE);
    if (!mem->ram) return -1;
    mem->size = GUEST_RAM_SIZE;
    asm_mem_zero(mem->ram, mem->size);
    return 0;
}

void vm_mem_destroy(vm_mem_t *mem) {
    if (!mem) return;
    if (mem->ram) {
        mem_domain_free(MEM_DOMAIN_USER, mem->ram);
        mem->ram = NULL;
    }
    mem->size = 0;
}

void vm_mem_zero(vm_mem_t *mem) {
    if (!mem || !mem->ram) return;
    asm_mem_zero(mem->ram, mem->size);
}

int vm_mem_load(vm_mem_t *mem, uint32_t guest_addr, const void *src, size_t n) {
    if (!mem || !mem->ram || !src) return -1;
    if (guest_addr + n > mem->size) return -1;
    asm_mem_copy(mem->ram + guest_addr, src, n);
    return 0;
}

int vm_mem_read(vm_mem_t *mem, uint32_t guest_addr, void *dst, size_t n) {
    if (!mem || !mem->ram || !dst) return -1;
    if (guest_addr + n > mem->size) return -1;
    asm_mem_copy(dst, mem->ram + guest_addr, n);
    return 0;
}

int vm_mem_write(vm_mem_t *mem, uint32_t guest_addr, const void *src, size_t n) {
    if (!mem || !mem->ram || !src) return -1;
    if (guest_addr + n > mem->size) return -1;
    asm_mem_copy(mem->ram + guest_addr, src, n);
    return 0;
}

uint8_t vm_mem_read8(vm_mem_t *mem, uint32_t guest_addr) {
    if (!mem || !mem->ram || guest_addr >= mem->size) return 0;
    return mem->ram[guest_addr];
}

uint16_t vm_mem_read16(vm_mem_t *mem, uint32_t guest_addr) {
    uint16_t v;
    if (vm_mem_read(mem, guest_addr, &v, 2) != 0) return 0;
    return v;
}

void vm_mem_write8(vm_mem_t *mem, uint32_t guest_addr, uint8_t v) {
    if (!mem || !mem->ram || guest_addr >= mem->size) return;
    mem->ram[guest_addr] = v;
}

void vm_mem_write16(vm_mem_t *mem, uint32_t guest_addr, uint16_t v) {
    vm_mem_write(mem, guest_addr, &v, 2);
}
