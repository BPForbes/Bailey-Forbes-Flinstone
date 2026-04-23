/**
 * AArch64 port I/O declarations — parallel to kernel/arch/x86_64/drivers/port_io.h.
 *
 * ARM has no x86-style port I/O (in/out instructions). These stubs satisfy the
 * fl_ioport HAL interface so that driver code compiles unchanged on both arches.
 * On bare-metal ARM, peripheral I/O uses memory-mapped I/O (fl_mmio_*) instead.
 */
#ifndef AARCH64_PORT_IO_H
#define AARCH64_PORT_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  port_inb (uint16_t port);
void     port_outb(uint16_t port, uint8_t  value);
uint16_t port_inw (uint16_t port);
void     port_outw(uint16_t port, uint16_t value);
uint32_t port_inl (uint16_t port);
void     port_outl(uint16_t port, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif /* AARCH64_PORT_IO_H */
