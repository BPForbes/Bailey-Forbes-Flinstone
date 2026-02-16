/**
 * Memory-Mapped I/O - Tier 1 (most modern hardware).
 * Works on both x86-64 and ARM. Used by PCI BARs, AHCI, NVMe, USB, APIC, HPET, GOP framebuffers.
 */
#ifndef MMIO_H
#define MMIO_H

#include <stdint.h>

/* Volatile access - compiler barrier implied. For strict ordering, add arch-specific barriers. */
static inline uint8_t  mmio_read8 (volatile void *addr) { return *(volatile uint8_t  *)addr; }
static inline uint16_t mmio_read16(volatile void *addr) { return *(volatile uint16_t *)addr; }
static inline uint32_t mmio_read32(volatile void *addr) { return *(volatile uint32_t *)addr; }

static inline void mmio_write8 (volatile void *addr, uint8_t  v) { *(volatile uint8_t  *)addr = v; }
static inline void mmio_write16(volatile void *addr, uint16_t v) { *(volatile uint16_t *)addr = v; }
static inline void mmio_write32(volatile void *addr, uint32_t v) { *(volatile uint32_t *)addr = v; }

#endif /* MMIO_H */
