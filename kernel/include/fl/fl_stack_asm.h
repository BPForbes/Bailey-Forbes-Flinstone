#ifndef FL_FL_STACK_ASM_H
#define FL_FL_STACK_ASM_H

#if defined(__aarch64__)
#include "../../arch/aarch64/include/fl_stack_asm.h"
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include "../../arch/x86_64/include/fl_stack_asm.h"
#else
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int     asm_fl_stack_push(void *stack, uintptr_t v);
int     asm_fl_stack_pop(void *stack, uintptr_t *out);
size_t  asm_fl_stack_count(const void *stack);
size_t  asm_fl_elev_count_active(void *slots, size_t max_slots, int64_t now_ns);
#ifdef __cplusplus
}
#endif
#endif

#endif /* FL_FL_STACK_ASM_H */
