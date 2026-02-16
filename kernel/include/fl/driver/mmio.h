/**
 * Memory-Mapped I/O - architecture-neutral.
 * Works on x86-64, ARM, VM. Used by PCI BARs, virtio MMIO, etc.
 */
#ifndef FL_DRIVER_MMIO_H
#define FL_DRIVER_MMIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint8_t  fl_mmio_read8 (volatile void *addr) {
    return *(volatile uint8_t *)addr;
}
static inline uint16_t fl_mmio_read16(volatile void *addr) {
    return *(volatile uint16_t *)addr;
}
static inline uint32_t fl_mmio_read32(volatile void *addr) {
    return *(volatile uint32_t *)addr;
}
static inline void fl_mmio_write8 (volatile void *addr, uint8_t  v) {
    *(volatile uint8_t *)addr = v;
}
static inline void fl_mmio_write16(volatile void *addr, uint16_t v) {
    *(volatile uint16_t *)addr = v;
}
static inline void fl_mmio_write32(volatile void *addr, uint32_t v) {
    *(volatile uint32_t *)addr = v;
}

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_MMIO_H */
