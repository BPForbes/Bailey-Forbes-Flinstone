#ifndef MEM_DOMAIN_H
#define MEM_DOMAIN_H

#include "fl/mem_asm.h"
#include <stddef.h>

typedef enum {
    MEM_DOMAIN_KERNEL,
    MEM_DOMAIN_DRIVER,
    MEM_DOMAIN_FS,
    MEM_DOMAIN_USER,
} mem_domain_t;

void *mem_domain_alloc(mem_domain_t domain, size_t size);
void *mem_domain_calloc(mem_domain_t domain, size_t nmemb, size_t size);
void *mem_domain_realloc(mem_domain_t domain, void *ptr, size_t size);
void mem_domain_free(mem_domain_t domain, void *ptr);

void mem_domain_copy(void *dst, const void *src, size_t n);
void mem_domain_zero(void *ptr, size_t n);
/* Secret wipe: fenced so the stores finish before the buffer is freed or reused. */
void mem_domain_secure_zero(void *ptr, size_t n);
void mem_domain_fill(void *ptr, unsigned char byte, size_t n);
int mem_domain_equal(const void *a, const void *b, size_t n);

#endif /* MEM_DOMAIN_H */
