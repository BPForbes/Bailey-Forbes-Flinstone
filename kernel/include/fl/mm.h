#ifndef FL_MM_H
#define FL_MM_H

#include <stddef.h>
#include <stdint.h>

#include "mem_domain.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Memory allocation */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);

/* Page management */
void *alloc_page(void);
void free_page(void *page);
size_t get_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_MM_H */
