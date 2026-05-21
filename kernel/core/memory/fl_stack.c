#include "fl_stack.h"
#include "fl/fl_stack_asm.h"

int fl_stack_init(fl_stack_t *s, uintptr_t *buf, size_t cap) {
    if (!s || !buf || cap == 0)
        return -1;
    s->data = buf;
    s->cap = cap;
    s->top = 0;
    return 0;
}

void fl_stack_clear(fl_stack_t *s) {
    if (s)
        s->top = 0;
}

int fl_stack_push(fl_stack_t *s, uintptr_t v) {
#if defined(FL_STACK_ASM_AVAILABLE)
    return asm_fl_stack_push(s, v);
#else
    if (!s || s->top >= s->cap)
        return -1;
    s->data[s->top++] = v;
    return 0;
#endif
}

int fl_stack_pop(fl_stack_t *s, uintptr_t *out) {
#if defined(FL_STACK_ASM_AVAILABLE)
    return asm_fl_stack_pop(s, out);
#else
    if (!s || s->top == 0)
        return -1;
    if (out)
        *out = s->data[--s->top];
    else
        s->top--;
    return 0;
#endif
}

int fl_stack_empty(const fl_stack_t *s) {
    return (!s || s->top == 0) ? 1 : 0;
}

size_t fl_stack_count(const fl_stack_t *s) {
#if defined(FL_STACK_ASM_AVAILABLE)
    return asm_fl_stack_count(s);
#else
    return s ? s->top : 0;
#endif
}
