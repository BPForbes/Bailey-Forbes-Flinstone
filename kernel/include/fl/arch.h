#ifndef FL_ARCH_H
#define FL_ARCH_H

/**
 * Architecture hook API boundary
 * 
 * This header defines the interface between architecture-independent
 * kernel code and architecture-specific implementations.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Architecture-specific initialization */
void arch_init(void);
void arch_shutdown(void);

/* Memory management hooks */
void *arch_kmalloc(size_t size);
void arch_kfree(void *ptr);
void *arch_krealloc(void *ptr, size_t size);

/* Memory primitives (architecture-specific optimized) */
void arch_memcpy(void *dst, const void *src, size_t n);
void arch_memset(void *ptr, int value, size_t n);
void arch_memzero(void *ptr, size_t n);

/* Interrupts */
void arch_enable_interrupts(void);
void arch_disable_interrupts(void);
int arch_irq_register(unsigned int irq, void (*handler)(void));
void arch_irq_unregister(unsigned int irq);

/* Context switching */
struct arch_context;
void arch_context_switch(struct arch_context *from, struct arch_context *to);
struct arch_context *arch_context_create(void (*entry)(void), void *stack);
void arch_context_destroy(struct arch_context *ctx);

/* CPU-specific */
void arch_cpu_idle(void);
uint64_t arch_get_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_ARCH_H */
