#include "idt.h"
#include "drivers.h"
#include <stdint.h>

/* Handler signature: void handler(uint64_t vector, uint64_t error_code)
 * For exceptions that don't push an error code, the stub pushes 0.
 * Handlers may ignore error_code if not applicable to their vector. */
typedef void (*idt_handler_t)(uint64_t vector, uint64_t error_code);

static idt_handler_t s_handlers[256];

int x86_idt_register_handler(int vector, idt_handler_t handler) {
    if (vector < 0 || vector >= 256 || !handler)
        return -1;
    s_handlers[vector] = handler;
    return 0;
}

/* Called from isr_common_stub with vector in %rdi and error_code in %rsi.
 * Interrupts are disabled by the CPU on entry to any interrupt gate. */
void x86_idt_dispatch(uint64_t vector, uint64_t error_code) {
    /* Unhandled CPU exception (0-31): panic to avoid re-executing faulting
     * instruction and potential triple-fault. */
    if (vector < 32u && !s_handlers[vector]) {
        /* Minimal panic: halt without display dependency (display may not be initialized) */
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }

    if (vector < 256u && s_handlers[vector])
        s_handlers[vector](vector, error_code);

    /* Send EOI for remapped hardware IRQs (PIC lines 0-15 -> vectors 32-47).
     * TODO: Add spurious IRQ detection for master IRQ7 (vec 0x27) and
     * slave IRQ15 (vec 0x2F) per Intel 8259A spec before sending EOI. */
    if (vector >= 0x20u && vector <= 0x2Fu) {
        if (g_pic_driver && g_pic_driver->eoi)
            g_pic_driver->eoi(g_pic_driver, (int)(vector - 0x20u));
    }
}
