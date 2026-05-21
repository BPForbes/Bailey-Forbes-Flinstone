#ifndef FL_EXEC_CONTEXT_H
#define FL_EXEC_CONTEXT_H

#include "contract_result.h"
#include <stddef.h>
#include <stdint.h>

#define FL_EXEC_GPR_COUNT 16u
#define FL_EXEC_STACK_DEFAULT 4096u

/** Hosted execution context (P1-1): heap backing, stack region, IP, GPR bank. */
typedef struct fl_exec_context {
    void     *heap_base;
    size_t    heap_size;
    size_t    heap_used;
    uint8_t  *stack_base;
    size_t    stack_size;
    size_t    stack_sp;
    uintptr_t ip;
    uintptr_t gpr[FL_EXEC_GPR_COUNT];
    int       alive;
} fl_exec_context_t;

fl_result_t fl_exec_context_create(fl_exec_context_t *ctx, size_t heap_bytes, size_t stack_bytes);
void        fl_exec_context_destroy(fl_exec_context_t *ctx);
fl_result_t fl_exec_context_reset(fl_exec_context_t *ctx);
fl_result_t fl_exec_context_push_stack(fl_exec_context_t *ctx, uintptr_t word);
fl_result_t fl_exec_context_pop_stack(fl_exec_context_t *ctx, uintptr_t *word);
fl_result_t fl_exec_context_heap_alloc(fl_exec_context_t *ctx, size_t nbytes, void **out);

#endif /* FL_EXEC_CONTEXT_H */
