#ifndef FL_CSTR_H
#define FL_CSTR_H

#include "fl/mem_asm.h"
#include <stddef.h>

static inline size_t fl_cstr_len(const char *s, size_t max_len) {
    size_t n = 0;
    if (!s)
        return 0;
    while (n < max_len && s[n])
        n++;
    return n;
}

static inline int fl_cstr_eq(const char *a, const char *b) {
    size_t i = 0;
    if (!a || !b)
        return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i];
}

static inline void fl_cstr_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0)
        return;
    size_t n = fl_cstr_len(src, dst_len - 1);
    if (n) {
        const uintptr_t dst_start = (uintptr_t)dst;
        const uintptr_t dst_end = dst_start + n;
        const uintptr_t src_start = (uintptr_t)src;
        const uintptr_t src_end = src_start + n;
        int overlaps = src && dst_start < src_end && dst_end > src_start;
        if (!overlaps) {
            asm_mem_copy(dst, src, n);
        } else if (dst_start <= src_start) {
            for (size_t i = 0; i < n; i++)
                dst[i] = src[i];
        } else {
            for (size_t i = n; i > 0; i--)
                dst[i - 1] = src[i - 1];
        }
    }
    dst[n] = '\0';
}

#endif /* FL_CSTR_H */
