#include "dir_asm.h"
#include "mem_asm.h"

void dir_asm_copy(void *dst, const void *src, size_t n) {
#ifdef MEM_ASM_DEBUG
    mem_asm_checked_copy(dst, src, n);
#else
    asm_mem_copy(dst, src, n);
#endif
}

void dir_asm_zero(void *ptr, size_t n) {
#ifdef MEM_ASM_DEBUG
    mem_asm_checked_zero(ptr, n);
#else
    asm_mem_zero(ptr, n);
#endif
}
