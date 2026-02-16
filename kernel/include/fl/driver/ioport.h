/**
 * Port I/O abstraction - HAL implements per platform.
 * x86-64: real port I/O. ARM: stubs. VM: emulated.
 */
#ifndef FL_DRIVER_IOPORT_H
#define FL_DRIVER_IOPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t  fl_ioport_in8 (uint16_t port);
uint16_t fl_ioport_in16(uint16_t port);
uint32_t fl_ioport_in32(uint16_t port);
void     fl_ioport_out8 (uint16_t port, uint8_t  v);
void     fl_ioport_out16(uint16_t port, uint16_t v);
void     fl_ioport_out32(uint16_t port, uint32_t v);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_IOPORT_H */
