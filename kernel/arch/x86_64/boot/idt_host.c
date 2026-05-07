#include "idt.h"

/* WASM host: no real IDT; bare-metal timer paths may still call idt_install(). */
void idt_install(void) { }
