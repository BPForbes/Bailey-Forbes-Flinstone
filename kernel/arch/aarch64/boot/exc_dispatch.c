#include "vectors.h"
#include "hal/arm_gic.h"
#include <stdint.h>

/* Exception slot indices for IRQ at each EL */
#define SLOT_EL1_IRQ  5      /* Current EL SP_EL1, IRQ */
#define SLOT_EL0_IRQ  9      /* Lower EL AArch64, IRQ  */
#define MAX_HANDLERS  1024u  /* GICv2 INTID space, including spurious 1023 */

static void (*s_handlers[MAX_HANDLERS])(void);

void aarch64_exc_register_handler(int slot, void (*handler)(void)) {
    /* Registration is a boot-time operation: callers must install handlers
     * before unmasking IRQs with DAIFCLR so dispatch can read lock-free. */
    if (slot >= 0 && (uint32_t)slot < MAX_HANDLERS)
        s_handlers[slot] = handler;
}

/* Called from each vector slot with the slot index in x0 (as uint64_t
 * per the AArch64 calling convention; narrowed to int safely since 0-15). */
void aarch64_exc_dispatch(uint64_t slot) {
    int n = (int)(slot & 0xFu);

    uint32_t irq_id = 0;
    int is_irq_slot = (n == SLOT_EL1_IRQ || n == SLOT_EL0_IRQ);
    if (is_irq_slot)
        irq_id = arm_gic_iar();

    uint32_t handler_id = is_irq_slot ? irq_id : (uint32_t)n;
    if (handler_id < MAX_HANDLERS && s_handlers[handler_id])
        s_handlers[handler_id]();

    if (is_irq_slot)
        arm_gic_eoi(irq_id);
}
