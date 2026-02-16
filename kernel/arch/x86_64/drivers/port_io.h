#ifndef PORT_IO_H
#define PORT_IO_H

#include <stdint.h>

/* Port I/O - Tier 2 (legacy PS/2, PIC, PIT). x86: real in/out. ARM: stubs.
 * Tier 1 = MMIO (mmio.h). PCI config uses port 0xCF8/0xCFC on x86 or ECAM (MMIO) on both. */
uint8_t  port_inb(uint16_t port);
void     port_outb(uint16_t port, uint8_t value);
uint16_t port_inw(uint16_t port);
void     port_outw(uint16_t port, uint16_t value);
uint32_t port_inl(uint16_t port);
void     port_outl(uint16_t port, uint32_t value);

#endif /* PORT_IO_H */
