#ifndef FL_STACK_H
#define FL_STACK_H

#include <stddef.h>
#include <stdint.h>

/** Fixed-capacity LIFO stack of uintptr_t (PMM free-frame indices, etc.). */
typedef struct fl_stack {
    uintptr_t *data;
    size_t     cap;
    size_t     top;
} fl_stack_t;

int  fl_stack_init(fl_stack_t *s, uintptr_t *buf, size_t cap);
void fl_stack_clear(fl_stack_t *s);
int  fl_stack_push(fl_stack_t *s, uintptr_t v);
int  fl_stack_pop(fl_stack_t *s, uintptr_t *out);
int  fl_stack_empty(const fl_stack_t *s);
size_t fl_stack_count(const fl_stack_t *s);

#endif /* FL_STACK_H */
