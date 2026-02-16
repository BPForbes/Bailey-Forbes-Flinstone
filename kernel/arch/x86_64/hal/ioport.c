/**
 * x86-64 HAL: Port I/O implementation.
 * Wraps arch-specific port_io.h to fulfill fl/driver/ioport.h contract.
 */
#include "fl/driver/ioport.h"
#include "drivers/port_io.h"

uint8_t  fl_ioport_in8 (uint16_t port) { return port_inb(port); }
uint16_t fl_ioport_in16(uint16_t port) { return port_inw(port); }
uint32_t fl_ioport_in32(uint16_t port) { return port_inl(port); }
void     fl_ioport_out8 (uint16_t port, uint8_t  v) { port_outb(port, v); }
void     fl_ioport_out16(uint16_t port, uint16_t v) { port_outw(port, v); }
void     fl_ioport_out32(uint16_t port, uint32_t v) { port_outl(port, v); }
