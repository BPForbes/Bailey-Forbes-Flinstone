/**
 * Interrupt controller abstraction.
 * x86: PIC/APIC. ARM: GIC. VM: synthetic IRQ routing.
 */
#ifndef FL_DRIVER_IRQ_H
#define FL_DRIVER_IRQ_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fl_irq_handler_t)(int irq, void *ctx);

void fl_irq_init(void);
void fl_irq_eoi(int irq);
int  fl_irq_register(int irq, fl_irq_handler_t handler, void *ctx);
void fl_irq_unregister(int irq);
void fl_irq_enable(int irq);
void fl_irq_disable(int irq);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_IRQ_H */
