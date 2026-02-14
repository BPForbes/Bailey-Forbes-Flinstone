/**
 * Memory domains - architectural virtualization.
 * Tag allocations by domain; use ASM primitives for buffer ops.
 * Domains share heap initially; abstraction allows future quotas/pools.
 */
#ifndef MEM_DOMAIN_H
#define MEM_DOMAIN_H

#include "mem_asm.h"
#include <stddef.h>

typedef enum {
    MEM_DOMAIN_KERNEL,
    MEM_DOMAIN_DRIVER,
    MEM_DOMAIN_FS,
    MEM_DOMAIN_USER,
} mem_domain_t;

/* Allocate in domain (uses malloc/calloc; domain is metadata for future) */
void *mem_domain_alloc(mem_domain_t domain, size_t size);
void *mem_domain_calloc(mem_domain_t domain, size_t nmemb, size_t size);
void mem_domain_free(mem_domain_t domain, void *ptr);

/* Buffer ops - always use ASM for hot path */
void mem_domain_copy(void *dst, const void *src, size_t n);
void mem_domain_zero(void *ptr, size_t n);
void mem_domain_fill(void *ptr, unsigned char byte, size_t n);

#endif /* MEM_DOMAIN_H */
