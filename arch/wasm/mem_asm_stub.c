#include "mem_asm.h"
#include <string.h>

void asm_mem_copy(void *dst, const void *src, size_t n) { memcpy(dst, src, n); }
void asm_mem_zero(void *ptr, size_t n)                  { memset(ptr, 0, n); }
void asm_block_fill(void *ptr, unsigned char byte, size_t n) { memset(ptr, byte, n); }
