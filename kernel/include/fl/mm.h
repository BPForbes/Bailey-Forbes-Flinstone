#ifndef FL_MM_H
#define FL_MM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory domain types */
typedef enum {
    MEM_DOMAIN_KERNEL,
    MEM_DOMAIN_USER,
    MEM_DOMAIN_FS,
    MEM_DOMAIN_MAX
} mem_domain_t;

/* Memory allocation */
void *kmalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t size);

/* Domain-specific allocation */
void *mem_domain_alloc(mem_domain_t domain, size_t size);
void mem_domain_free(mem_domain_t domain, void *ptr);

/* Page management */
void *alloc_page(void);
void free_page(void *page);
size_t get_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_MM_H */
