#ifndef MEM_ASM_H
#define MEM_ASM_H

/**
 * Architecture-aware mem_asm.h wrapper
 * 
 * This header automatically includes the correct architecture-specific
 * mem_asm.h based on the build configuration.
 */

#include <stddef.h>

/* Include architecture-specific header based on architecture defines */
#if defined(TARGET_ARCH_x86_64) || defined(__x86_64__) || defined(_M_X64)
    #include "kernel/arch/x86_64/include/mem_asm.h"
#elif defined(TARGET_ARCH_aarch64) || defined(__aarch64__) || defined(_M_ARM64)
    #include "kernel/arch/aarch64/include/mem_asm.h"
#else
    /* Fallback: assume x86_64 */
    #warning "Architecture not detected. Assuming x86_64. Define TARGET_ARCH_x86_64 or TARGET_ARCH_aarch64 to be explicit."
    #include "kernel/arch/x86_64/include/mem_asm.h"
#endif

#endif /* MEM_ASM_H */
