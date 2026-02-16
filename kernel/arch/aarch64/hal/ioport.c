/**
 * AArch64 HAL: Port I/O stubs (ARM uses MMIO only).
 */
#include "fl/driver/ioport.h"

uint8_t  fl_ioport_in8 (uint16_t port) { (void)port; return 0xFF; }
uint16_t fl_ioport_in16(uint16_t port) { (void)port; return 0xFFFF; }
uint32_t fl_ioport_in32(uint16_t port) { (void)port; return 0xFFFFFFFF; }
void     fl_ioport_out8 (uint16_t port, uint8_t  v) { (void)port; (void)v; }
void     fl_ioport_out16(uint16_t port, uint16_t v) { (void)port; (void)v; }
void     fl_ioport_out32(uint16_t port, uint32_t v) { (void)port; (void)v; }
