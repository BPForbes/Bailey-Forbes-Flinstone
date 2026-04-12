/**
 * Kernel allocator interface.
 *
 * Provides kmalloc/kfree/krealloc as the stable kernel-level memory API.
 * Backing store:
 *   - Host / default build: libc malloc/realloc/free
 *   - USE_ASM_ALLOC=1 build:  ASM allocator in arch/<arch>/gas/alloc_*.s;
 *     malloc/free are re-implemented in assembly and linked in place of libc;
 *     this translation unit just forwards to whichever malloc is linked.
 *
 * Drivers MUST use kmalloc/kfree rather than calling malloc/free directly so
 * that the kernel can swap the allocator without touching every driver.
 */
#include "fl/mm.h"
#include <stdlib.h>

void *kmalloc(size_t size) {
    return mem_domain_alloc(MEM_DOMAIN_KERNEL, size);
}

void kfree(void *ptr) {
    mem_domain_free(MEM_DOMAIN_KERNEL, ptr);
}

void *krealloc(void *ptr, size_t size) {
    return mem_domain_realloc(MEM_DOMAIN_KERNEL, ptr, size);
}

/* Page helpers - thin wrappers; a real MM would manage page frames */
void *alloc_page(void) {
    /* Allocate a page-aligned buffer (4,096 bytes) using aligned_alloc.
     * This ensures 4 KiB alignment required for page-frame semantics,
     * which kmalloc(get_page_size()) via libc malloc cannot guarantee.
     */
    size_t page_size = get_page_size();
    void *page = aligned_alloc(page_size, page_size);
    return page;  /* returns NULL on failure */
}

void free_page(void *page) {
    kfree(page);
}

size_t get_page_size(void) {
    return 4096;
}