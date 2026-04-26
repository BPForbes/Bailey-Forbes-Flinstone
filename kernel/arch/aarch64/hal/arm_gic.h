/**
 * ARM GIC (Generic Interrupt Controller) - minimal init/EOI.
 */
#ifndef ARM_GIC_H
#define ARM_GIC_H

#include <stdint.h>

void arm_gic_init(void);
uint32_t arm_gic_iar(void);
void arm_gic_eoi(uint32_t irq);

#endif /* ARM_GIC_H */
