#ifndef FL_STACK_ASM_H
#define FL_STACK_ASM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ASM-backed fl_stack_t hot path (x86-64).
 * C struct layout: { uintptr_t *data; size_t cap; size_t top; } — 24 bytes on LP64.
 */
int     asm_fl_stack_push(void *stack, uintptr_t v);
int     asm_fl_stack_pop(void *stack, uintptr_t *out);
size_t  asm_fl_stack_count(const void *stack);

/** elevation slot stride 56: used@48, expires_ns@40 (see elevation.c). */
size_t  asm_fl_elev_count_active(void *slots, size_t max_slots, int64_t now_ns);

#ifdef __cplusplus
}
#endif

#endif /* FL_STACK_ASM_H */
