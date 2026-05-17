/**
 * ASM-backed memory helpers for **P0** contract hot paths (log sink dispatch,
 * audit ring moves). Thin wrappers over **asm_mem_zero** / **asm_mem_copy** so
 * contract code depends on **fl/mem_asm.h** resolution (GAS/NASM/AArch64).
 */
#ifndef FL_CONTRACT_ASM_H
#define FL_CONTRACT_ASM_H

#include "fl/mem_asm.h"
#include <stddef.h>

static inline void fl_contract_mem_zero(void *dst, size_t n) {
    asm_mem_zero(dst, n);
}

static inline void fl_contract_mem_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}

#endif /* FL_CONTRACT_ASM_H */
