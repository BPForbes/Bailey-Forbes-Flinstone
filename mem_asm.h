#ifndef MEM_ASM_WRAPPER_H
#define MEM_ASM_WRAPPER_H

/**
 * Architecture-aware mem_asm.h wrapper
 * 
 * This header automatically includes the correct architecture-specific
 * mem_asm.h based on the build configuration.
 */

#include <stddef.h>

/* Include architecture-specific header based on architecture defines */
#if defined(TARGET_ARCH_aarch64) || defined(__aarch64__) || defined(_M_ARM64)
    #include "kernel/arch/aarch64/include/mem_asm.h"
#else
    /* x86_64 (default) */
    #include "kernel/arch/x86_64/include/mem_asm.h"
#endif

#endif /* MEM_ASM_WRAPPER_H */
