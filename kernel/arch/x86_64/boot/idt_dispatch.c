#include "idt.h"
#include "drivers.h"
#include <stdint.h>

static void (*s_handlers[256])(void);

int x86_idt_register_handler(int vector, void (*handler)(void)) {
    if (vector < 0 || vector >= 256 || !handler)
        return -1;
    s_handlers[vector] = handler;
    return 0;
}

/* Called from isr_common_stub with the vector number in %rdi (SysV AMD64 ABI).
 * Interrupts are disabled by the CPU on entry to any interrupt gate. */
void x86_idt_dispatch(uint64_t vector) {
    if (vector < 256u && s_handlers[vector])
        s_handlers[vector]();

    /* Send EOI for remapped hardware IRQs (PIC lines 0-15 -> vectors 32-47). */
    if (vector >= 0x20u && vector <= 0x2Fu) {
        if (g_pic_driver && g_pic_driver->eoi)
            g_pic_driver->eoi(g_pic_driver, (int)(vector - 0x20u));
    }
}
