#include "mem_domain.h"
#include <stdlib.h>
#include <string.h>

/* For now, domain is metadata only - same heap */
void *mem_domain_alloc(mem_domain_t domain, size_t size) {
    (void)domain;
    return malloc(size);
}

void *mem_domain_calloc(mem_domain_t domain, size_t nmemb, size_t size) {
    (void)domain;
    return calloc(nmemb, size);
}

void mem_domain_free(mem_domain_t domain, void *ptr) {
    (void)domain;
    free(ptr);
}

void mem_domain_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}

void mem_domain_zero(void *ptr, size_t n) {
    asm_mem_zero(ptr, n);
}

void mem_domain_fill(void *ptr, unsigned char byte, size_t n) {
    asm_block_fill(ptr, byte, n);
}
