#ifndef X86_64_IDT_H
#define X86_64_IDT_H

/* Load the 256-entry IDT and enable the common ISR stubs.
 * Must be called after gdt_install() and pic_driver init. */
void idt_install(void);

/* Register a C handler for the given interrupt vector (0-255).
 * The handler is called with interrupts disabled from isr_common_stub. */
void x86_idt_register_handler(int vector, void (*handler)(void));

#endif /* X86_64_IDT_H */
