#include "idt.h"

/**
 * Install the architecture-specific Interrupt Descriptor Table (IDT) used during early x86_64 boot.
 *
 * On this host build the function is a no-op; platforms that require an IDT should
 * provide an implementation that initializes IDT entries and loads the IDT register.
 */
void idt_install(void) { }
