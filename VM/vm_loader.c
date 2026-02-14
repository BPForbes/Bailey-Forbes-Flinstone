#include "vm_loader.h"
#include "mem_domain.h"
#include "mem_asm.h"
#include <stdio.h>
#include <stdlib.h>

int vm_load_binary(vm_mem_t *mem, uint32_t addr, const void *data, size_t len) {
    if (!mem || !mem->ram || !data) return -1;
    if (addr + len > mem->size) return -1;
    asm_mem_copy(mem->ram + addr, data, len);
    return 0;
}

int vm_load_file(vm_mem_t *mem, uint32_t addr, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || addr + (size_t)sz > mem->size) {
        fclose(f);
        return -1;
    }
    void *buf = mem_domain_alloc(MEM_DOMAIN_USER, (size_t)sz);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        mem_domain_free(MEM_DOMAIN_USER, buf);
        return -1;
    }
    asm_mem_copy(mem->ram + addr, buf, (size_t)sz);
    mem_domain_free(MEM_DOMAIN_USER, buf);
    return 0;
}
