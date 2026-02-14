#include "dir_asm.h"
#include "mem_asm.h"

void dir_asm_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}

void dir_asm_zero(void *ptr, size_t n) {
    asm_mem_zero(ptr, n);
}
