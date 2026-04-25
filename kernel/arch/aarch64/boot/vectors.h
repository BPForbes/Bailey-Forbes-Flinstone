#ifndef AARCH64_VECTORS_H
#define AARCH64_VECTORS_H

/* Install the EL1 exception vector table by writing VBAR_EL1.
 * Call after arm_gic_init() and before enabling interrupts. */
void arm_vbar_install(void);

/* Register a C handler for exception slot n (0-15).
 * Slots 5 and 9 are IRQ from EL1/EL0 respectively. */
void aarch64_exc_register_handler(int slot, void (*handler)(void));

#endif /* AARCH64_VECTORS_H */
