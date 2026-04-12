#ifndef FL_MEM_ASM_H
#define FL_MEM_ASM_H

/* Stable include for drivers — resolves arch-specific mem_asm.h. */
#if defined(__aarch64__)
#include "../../arch/aarch64/include/mem_asm.h"
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include "../../arch/x86_64/include/mem_asm.h"
#else
#include "../../../mem_asm.h"
#endif

#endif /* FL_MEM_ASM_H */
