#ifndef FL_MM_H
#define FL_MM_H

#include <stddef.h>
#include <stdint.h>

#include "mem_domain.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fl_device;

typedef struct fl_dma_info {
    void *ptr;
    size_t size;
    const struct fl_device *owner;
} fl_dma_info_t;

/* Memory allocation */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);
void *fl_dma_alloc(size_t size);
void *fl_dma_alloc_device(struct fl_device *dev, size_t size);
void fl_dma_free(void *ptr);
int fl_dma_get_info(void *ptr, fl_dma_info_t *out);
int fl_dma_allocation_count(void);
void fl_dma_zero(void *ptr, size_t size);
void fl_dma_copy(void *dst, const void *src, size_t size);

/* Page management */
void *alloc_page(void);
void free_page(void *page);
size_t get_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_MM_H */
