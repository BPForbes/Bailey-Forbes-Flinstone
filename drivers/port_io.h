#ifndef PORT_IO_H
#define PORT_IO_H

#include <stdint.h>

/* ASM primitives - direct hardware port access.
 * On bare-metal: works when I/O privilege is set.
 * On Linux userspace: requires ioperm(0, 65536, 1) or root. */
uint8_t  port_inb(uint16_t port);
void     port_outb(uint16_t port, uint8_t value);
uint16_t port_inw(uint16_t port);
void     port_outw(uint16_t port, uint16_t value);

#endif /* PORT_IO_H */
