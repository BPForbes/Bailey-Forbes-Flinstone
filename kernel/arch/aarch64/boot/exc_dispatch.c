#include "vectors.h"
#include "hal/arm_gic.h"
#include <stdint.h>

/* Exception slot indices for IRQ at each EL */
#define SLOT_EL1_IRQ  5   /* Current EL SP_EL1, IRQ */
#define SLOT_EL0_IRQ  9   /* Lower EL AArch64, IRQ  */

static void (*s_handlers[16])(void);

void aarch64_exc_register_handler(int slot, void (*handler)(void)) {
    if (slot >= 0 && slot < 16)
        s_handlers[slot] = handler;
}

/* Called from each vector slot with the slot index in x0 (as uint64_t
 * per the AArch64 calling convention; narrowed to int safely since 0-15). */
void aarch64_exc_dispatch(uint64_t slot) {
    int n = (int)(slot & 0xFu);

    if (s_handlers[n])
        s_handlers[n]();

    /* Send GIC EOI for hardware IRQ slots — the GIC IAR is not read here
     * so we pass the slot index as a placeholder; a real driver would read
     * GICC_IAR to obtain the actual interrupt ID before calling eoi(). */
    if (n == SLOT_EL1_IRQ || n == SLOT_EL0_IRQ)
        arm_gic_eoi(n);
}
