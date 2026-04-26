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
 * Page allocation:
 *   - DRIVERS_BAREMETAL: pmm_alloc_frame() / pmm_free_frame() — tracked
 *     physical frames from the static PMM bitfield.
 *   - Host: aligned_alloc(4096, 4096) — virtual memory from libc heap.
 *
 * Drivers MUST use kmalloc/kfree rather than calling malloc/free directly so
 * that the kernel can swap the allocator without touching every driver.
 */
#include "fl/mm.h"
#include "pmm.h"
#ifndef DRIVERS_BAREMETAL
#include <stdlib.h>
#endif

void *kmalloc(size_t size) {
    return mem_domain_alloc(MEM_DOMAIN_KERNEL, size);
}

void kfree(void *ptr) {
    mem_domain_free(MEM_DOMAIN_KERNEL, ptr);
}

void *krealloc(void *ptr, size_t size) {
    return mem_domain_realloc(MEM_DOMAIN_KERNEL, ptr, size);
}

size_t get_page_size(void) {
    return PMM_FRAME_SIZE;
}

#ifdef DRIVERS_BAREMETAL

void *alloc_page(void) {
    uintptr_t addr = pmm_alloc_frame();
    return addr ? (void *)addr : (void *)0;
}

void free_page(void *page) {
    if (page)
        pmm_free_frame((uintptr_t)page);
}

#else

void *alloc_page(void) {
    return aligned_alloc(PMM_FRAME_SIZE, PMM_FRAME_SIZE);
}

void free_page(void *page) {
    if (page)
        free(page);
}

#endif
