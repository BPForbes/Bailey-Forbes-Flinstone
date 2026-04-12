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
    return malloc(size);
}

void kfree(void *ptr) {
    free(ptr);
}

void *krealloc(void *ptr, size_t size) {
    return realloc(ptr, size);
}

/* Page helpers - thin wrappers; a real MM would manage page frames */
void *alloc_page(void) {
    return kmalloc(get_page_size());
}

void free_page(void *page) {
    kfree(page);
}

size_t get_page_size(void) {
    return 4096;
}
