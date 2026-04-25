#ifndef AARCH64_VECTORS_H
#define AARCH64_VECTORS_H

/* Install the EL1 exception vector table by writing VBAR_EL1.
 * Call after arm_gic_init() and before enabling interrupts. */
void arm_vbar_install(void);

/* Register a C handler for exception slot n (0-15), excluding IRQ slots.
 * Register handlers before unmasking CPU interrupts with DAIFCLR.
 * Slots 5 and 9 are IRQ entries and must NOT be registered via this API;
 * use aarch64_irq_register_handler for actual GIC INTID handlers. */
void aarch64_exc_register_handler(int slot, void (*handler)(void));

/* Register a C handler for a GIC INTID (0-1023).
 * Call before unmasking interrupts.  Separate from exception slots to
 * avoid collisions (e.g., exception slot 5 vs. GIC INTID 5). */
void aarch64_irq_register_handler(int intid, void (*handler)(void));

#endif /* AARCH64_VECTORS_H */
