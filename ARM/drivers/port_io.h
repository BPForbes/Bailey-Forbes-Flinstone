#ifndef PORT_IO_H
#define PORT_IO_H

#include <stdint.h>

/* ASM primitives - Note: ARM doesn't have port I/O like x86.
 * This is a stub implementation. For real ARM hardware, use MMIO
 * or platform-specific I/O functions. */
uint8_t  port_inb(uint16_t port);
void     port_outb(uint16_t port, uint8_t value);
uint16_t port_inw(uint16_t port);
void     port_outw(uint16_t port, uint16_t value);

#endif /* PORT_IO_H */
