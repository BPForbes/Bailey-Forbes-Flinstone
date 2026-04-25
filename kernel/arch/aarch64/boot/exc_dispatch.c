#include "vectors.h"
#include "hal/arm_gic.h"
#include <stdint.h>

/* Exception slot indices for IRQ at each EL */
#define SLOT_EL1_IRQ  5      /* Current EL SP_EL1, IRQ */
#define SLOT_EL0_IRQ  9      /* Lower EL AArch64, IRQ  */
#define MAX_EXC_SLOTS 16u    /* Exception vector table has 16 entries (0-15) */
#define MAX_IRQ_INTIDS 1024u /* GICv2 INTID space (0-1023, including spurious 1020-1023) */

/* Separate handler tables: exception slots vs. GIC IRQ INTIDs */
static void (*s_handlers[MAX_EXC_SLOTS])(void);     /* slots 0-15 */
static void (*irq_handlers[MAX_IRQ_INTIDS])(void);  /* INTIDs 0-1023 */

void aarch64_exc_register_handler(int slot, void (*handler)(void)) {
    /* Registration is a boot-time operation: callers must install handlers
     * before unmasking IRQs with DAIFCLR so dispatch can read lock-free.
     * Only register non-IRQ exception slots (0-15, excluding IRQ slots 5 and 9). */
    if (slot < 0 || (uint32_t)slot >= MAX_EXC_SLOTS)
        return;
    if (slot == SLOT_EL1_IRQ || slot == SLOT_EL0_IRQ)
        return; /* IRQ slots must use aarch64_irq_register_handler */
    s_handlers[slot] = handler;
}

void aarch64_irq_register_handler(int intid, void (*handler)(void)) {
    /* Register a handler for a GIC INTID (0-1023).
     * Call before unmasking IRQs. */
    if (intid >= 0 && (uint32_t)intid < MAX_IRQ_INTIDS)
        irq_handlers[intid] = handler;
}

/* Called from each vector slot with the slot index in x0 (as uint64_t
 * per the AArch64 calling convention; narrowed to int safely since 0-15). */
void aarch64_exc_dispatch(uint64_t slot) {
    int n = (int)(slot & 0xFu);

    uint32_t irq_id = 0;
    int is_irq_slot = (n == SLOT_EL1_IRQ || n == SLOT_EL0_IRQ);
    if (is_irq_slot) {
        irq_id = arm_gic_iar();
        /* Check for spurious IRQ (GICv2 spurious INTID range: 1020-1023).
         * Do not dispatch or EOI spurious interrupts. */
        if (irq_id >= 1020u && irq_id <= 1023u)
            return;
        /* Dispatch IRQ handler from separate irq_handlers table. */
        if (irq_id < MAX_IRQ_INTIDS && irq_handlers[irq_id])
            irq_handlers[irq_id]();
        arm_gic_eoi(irq_id);
    } else {
        /* Non-IRQ exception: dispatch from exception slot table. */
        if ((uint32_t)n < MAX_EXC_SLOTS && s_handlers[n])
            s_handlers[n]();
    }
}
