/**
 * Memory domain allocator.
 *
 * Host builds: thin wrappers over libc malloc/free — unchanged behaviour.
 *
 * DRIVERS_BAREMETAL builds: each domain is backed by a dedicated static BSS
 * arena.  Allocations use a bump pointer; freed blocks are returned to a
 * per-domain free list (first-fit).  Every allocation is preceded by a
 * size_t header so free() and realloc() know the block size without an
 * external table.
 *
 * Alignment: all allocations are rounded to ARENA_ALIGN (8) bytes.
 */
#include "mem_domain.h"
#include "fl/mem_asm.h"

#ifdef DRIVERS_BAREMETAL

#define ARENA_ALIGN   8u
#define HDR_SIZE      (sizeof(size_t))   /* stored immediately before payload */

/* Arena sizes per domain (tune as needed) */
#define ARENA_SZ_KERNEL (64u  * 1024u)
#define ARENA_SZ_DRIVER (64u  * 1024u)
#define ARENA_SZ_FS     (128u * 1024u)
#define ARENA_SZ_USER   (32u  * 1024u)

static uint8_t s_arena_kernel[ARENA_SZ_KERNEL];
static uint8_t s_arena_driver[ARENA_SZ_DRIVER];
static uint8_t s_arena_fs    [ARENA_SZ_FS];
static uint8_t s_arena_user  [ARENA_SZ_USER];

typedef struct free_node {
    struct free_node *next;
    size_t            data_size; /* size of the payload (not including this header) */
} free_node_t;

typedef struct {
    uint8_t     *base;
    size_t       capacity;
    size_t       bump;       /* next free byte offset from base */
    free_node_t *freelist;
} arena_t;

static arena_t s_arenas[4] = {
    { s_arena_kernel, ARENA_SZ_KERNEL, 0, (void*)0 },
    { s_arena_driver, ARENA_SZ_DRIVER, 0, (void*)0 },
    { s_arena_fs,     ARENA_SZ_FS,     0, (void*)0 },
    { s_arena_user,   ARENA_SZ_USER,   0, (void*)0 },
};

static arena_t *arena_for(mem_domain_t domain) {
    if ((unsigned)domain >= 4u) domain = MEM_DOMAIN_KERNEL;
    return &s_arenas[(int)domain];
}

static size_t align_up(size_t n) {
    return (n + ARENA_ALIGN - 1u) & ~(ARENA_ALIGN - 1u);
}

void *mem_domain_alloc(mem_domain_t domain, size_t size) {
    if (!size) return (void*)0;
    arena_t *a = arena_for(domain);
    size_t need = align_up(HDR_SIZE + size);

    /* Search free list (first-fit) */
    free_node_t **pp = &a->freelist;
    while (*pp) {
        free_node_t *node = *pp;
        if (node->data_size >= size) {
            *pp = node->next;           /* unlink */
            size_t *hdr = (size_t *)(void *)node;
            *hdr = node->data_size;     /* restore payload size in header */
            return (uint8_t *)(void *)hdr + HDR_SIZE;
        }
        pp = &(*pp)->next;
    }

    /* Bump allocate */
    if (a->bump + need > a->capacity) return (void*)0;
    size_t *hdr = (size_t *)(void *)(a->base + a->bump);
    *hdr = size;
    a->bump += need;
    return (uint8_t *)(void *)hdr + HDR_SIZE;
}

void *mem_domain_calloc(mem_domain_t domain, size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = mem_domain_alloc(domain, total);
    if (p) asm_mem_zero(p, total);
    return p;
}

void *mem_domain_realloc(mem_domain_t domain, void *ptr, size_t size) {
    if (!ptr) return mem_domain_alloc(domain, size);
    if (!size) { mem_domain_free(domain, ptr); return (void*)0; }
    size_t old_size = *((size_t *)((uint8_t *)ptr - HDR_SIZE));
    void *np = mem_domain_alloc(domain, size);
    if (!np) return (void*)0;
    size_t copy_n = old_size < size ? old_size : size;
    asm_mem_copy(np, ptr, copy_n);
    mem_domain_free(domain, ptr);
    return np;
}

void mem_domain_free(mem_domain_t domain, void *ptr) {
    if (!ptr) return;
    arena_t *a = arena_for(domain);
    size_t *hdr = (size_t *)((uint8_t *)ptr - HDR_SIZE);
    size_t data_size = *hdr;
    /* Re-use the block header region as a free_node_t */
    free_node_t *node = (free_node_t *)(void *)hdr;
    node->data_size = data_size;
    node->next = a->freelist;
    a->freelist = node;
}

#else /* HOST mode — keep libc behaviour */

#include <stdlib.h>

void *mem_domain_alloc(mem_domain_t domain, size_t size) {
    (void)domain;
    return malloc(size);
}

void *mem_domain_calloc(mem_domain_t domain, size_t nmemb, size_t size) {
    (void)domain;
    return calloc(nmemb, size);
}

void *mem_domain_realloc(mem_domain_t domain, void *ptr, size_t size) {
    (void)domain;
    return realloc(ptr, size);
}

void mem_domain_free(mem_domain_t domain, void *ptr) {
    (void)domain;
    free(ptr);
}

#endif /* DRIVERS_BAREMETAL */

/* Buffer ops — always use ASM regardless of mode */
void mem_domain_copy(void *dst, const void *src, size_t n) {
    asm_mem_copy(dst, src, n);
}

void mem_domain_zero(void *ptr, size_t n) {
    asm_mem_zero(ptr, n);
}

void mem_domain_fill(void *ptr, unsigned char byte, size_t n) {
    asm_block_fill(ptr, byte, n);
}
