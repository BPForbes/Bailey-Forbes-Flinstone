/**
 * ARM GIC (Generic Interrupt Controller) - minimal init/EOI.
 */
#ifndef ARM_GIC_H
#define ARM_GIC_H

void arm_gic_init(void);
void arm_gic_eoi(int irq);

#endif /* ARM_GIC_H */
