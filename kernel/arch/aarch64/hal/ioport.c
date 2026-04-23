/**
 * AArch64 HAL: Port I/O — routes through arch ASM stubs.
 *
 * Mirrors the x86_64 HAL pattern: fl_ioport_* calls forward to the
 * port_inb/port_outb/... functions defined in
 * kernel/arch/aarch64/drivers/port_io.S, which return the standard
 * "no device" sentinels (0xFF / 0xFFFF / 0xFFFFFFFF) since ARM has
 * no x86-style in/out instructions. On bare-metal ARM, actual peripheral
 * I/O goes through fl_mmio_* (memory-mapped I/O) instead.
 */
#include "fl/driver/ioport.h"
#include "drivers/port_io.h"

uint8_t  fl_ioport_in8 (uint16_t port) { return port_inb(port); }
uint16_t fl_ioport_in16(uint16_t port) { return port_inw(port); }
uint32_t fl_ioport_in32(uint16_t port) { return port_inl(port); }
void     fl_ioport_out8 (uint16_t port, uint8_t  v) { port_outb(port, v); }
void     fl_ioport_out16(uint16_t port, uint16_t v) { port_outw(port, v); }
void     fl_ioport_out32(uint16_t port, uint32_t v) { port_outl(port, v); }
