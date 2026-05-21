/* TODO(P0/Codex): P0-5 x86 IDT/tick full integration evidence on target hardware —
 * dispatch path exists; hosted CI does not claim bare-metal P0-8 completeness. */
#include "idt.h"
#include "drivers.h"
#include <stdint.h>

static x86_idt_handler_t s_handlers[256];

int x86_idt_register_handler(int vector, x86_idt_handler_t handler) {
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
     * Spurious IRQ7 / IRQ15 handling lives in the bare-metal PIC driver (8259A). */
    if (vector >= 0x20u && vector <= 0x2Fu) {
        if (g_pic_driver && g_pic_driver->eoi)
            g_pic_driver->eoi(g_pic_driver, (int)(vector - 0x20u));
    }
}
