#ifndef DIR_ASM_H
#define DIR_ASM_H

#include <stddef.h>

/* ASM-backed directory memory management.
 * Low-level buffer ops use ASM; higher logic (path resolution, dir creation) in C. */
void dir_asm_copy(void *dst, const void *src, size_t n);
void dir_asm_zero(void *ptr, size_t n);

#endif /* DIR_ASM_H */
