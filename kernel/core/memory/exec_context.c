#include "exec_context.h"
#include "mem_domain.h"
#include <stdlib.h>
#include <string.h>

fl_result_t fl_exec_context_create(fl_exec_context_t *ctx, size_t heap_bytes, size_t stack_bytes) {
    if (!ctx)
        return FL_RESULT_INVAL;
    mem_domain_zero(ctx, sizeof(*ctx));
    if (heap_bytes == 0)
        heap_bytes = 4096u;
    if (stack_bytes == 0)
        stack_bytes = FL_EXEC_STACK_DEFAULT;
    ctx->heap_base = malloc(heap_bytes);
    if (!ctx->heap_base)
        return FL_RESULT_NOMEM;
    ctx->heap_size = heap_bytes;
    ctx->stack_base = malloc(stack_bytes);
    if (!ctx->stack_base) {
        free(ctx->heap_base);
        mem_domain_zero(ctx, sizeof(*ctx));
        return FL_RESULT_NOMEM;
    }
    ctx->stack_size = stack_bytes;
    ctx->stack_sp = stack_bytes;
    ctx->ip = 0;
    ctx->alive = 1;
    return FL_RESULT_OK;
}

void fl_exec_context_destroy(fl_exec_context_t *ctx) {
    if (!ctx)
        return;
    free(ctx->heap_base);
    free(ctx->stack_base);
    mem_domain_zero(ctx, sizeof(*ctx));
}

fl_result_t fl_exec_context_reset(fl_exec_context_t *ctx) {
    if (!ctx || !ctx->alive)
        return FL_RESULT_INVAL;
    ctx->heap_used = 0;
    ctx->stack_sp = ctx->stack_size;
    ctx->ip = 0;
    mem_domain_zero(ctx->gpr, sizeof(ctx->gpr));
    return FL_RESULT_OK;
}

fl_result_t fl_exec_context_push_stack(fl_exec_context_t *ctx, uintptr_t word) {
    if (!ctx || !ctx->alive || !ctx->stack_base)
        return FL_RESULT_INVAL;
    if (ctx->stack_sp > ctx->stack_size || ctx->stack_sp < sizeof(uintptr_t))
        return FL_RESULT_NOMEM;
    ctx->stack_sp -= sizeof(uintptr_t);
    memcpy(ctx->stack_base + ctx->stack_sp, &word, sizeof(word));
    return FL_RESULT_OK;
}

fl_result_t fl_exec_context_pop_stack(fl_exec_context_t *ctx, uintptr_t *word) {
    if (!ctx || !ctx->alive || !ctx->stack_base)
        return FL_RESULT_INVAL;
    if (ctx->stack_sp > ctx->stack_size || ctx->stack_sp + sizeof(uintptr_t) > ctx->stack_size)
        return FL_RESULT_ERR;
    if (word)
        memcpy(word, ctx->stack_base + ctx->stack_sp, sizeof(*word));
    ctx->stack_sp += sizeof(uintptr_t);
    return FL_RESULT_OK;
}

fl_result_t fl_exec_context_heap_alloc(fl_exec_context_t *ctx, size_t nbytes, void **out) {
    size_t align = sizeof(uintptr_t);
    size_t pad;
    void *p;
    if (!ctx || !ctx->alive || !out)
        return FL_RESULT_INVAL;
    if (nbytes == 0)
        return FL_RESULT_INVAL;
    pad = (align - (ctx->heap_used % align)) % align;
    if (pad > ctx->heap_size - ctx->heap_used)
        return FL_RESULT_NOMEM;
    if (nbytes > ctx->heap_size - ctx->heap_used - pad)
        return FL_RESULT_NOMEM;
    ctx->heap_used += pad;
    p = (uint8_t *)ctx->heap_base + ctx->heap_used;
    ctx->heap_used += nbytes;
    *out = p;
    return FL_RESULT_OK;
}
