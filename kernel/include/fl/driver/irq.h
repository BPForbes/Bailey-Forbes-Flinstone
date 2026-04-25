/**
 * Interrupt controller abstraction.
 * x86: PIC/APIC. ARM: GIC. VM: synthetic IRQ routing.
 */
#ifndef FL_DRIVER_IRQ_H
#define FL_DRIVER_IRQ_H

#include <stdint.h>
#include "bus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fl_irq_handler_t)(int irq, void *ctx);

typedef struct fl_irq_info {
    int irq;
    int enabled;
    uint64_t dispatch_count;
    uint64_t eoi_count;
    const fl_device_t *owner;
} fl_irq_info_t;

void fl_irq_init(void);
void fl_irq_eoi(int irq);
int  fl_irq_register(int irq, fl_irq_handler_t handler, void *ctx);
int  fl_irq_register_device(fl_device_t *dev, int irq_resource_index, fl_irq_handler_t handler, void *ctx);
void fl_irq_unregister(int irq);
void fl_irq_unregister_device(fl_device_t *dev);
void fl_irq_enable(int irq);
void fl_irq_disable(int irq);
int  fl_irq_dispatch(int irq);
int  fl_irq_get_info(int irq, fl_irq_info_t *out);
unsigned long fl_irq_dispatch_count(int irq);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_IRQ_H */
