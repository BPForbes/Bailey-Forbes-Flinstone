/**
 * PCI config space implementation.
 * x86: legacy port 0xCF8/0xCFC or ECAM if base set.
 * ARM: ECAM only.
 */
#include "fl/driver/pci.h"
#include "port_io.h"
#include "mmio.h"
#include <stddef.h>

#ifndef DRIVERS_BAREMETAL
/* Host: no direct hardware access - stub */
static uint8_t  pci_stub_read8 (uint8_t b, uint8_t d, uint8_t f, uint8_t o) { (void)b;(void)d;(void)f;(void)o; return 0; }
static uint16_t pci_stub_read16(uint8_t b, uint8_t d, uint8_t f, uint8_t o) { (void)b;(void)d;(void)f;(void)o; return 0; }
static uint32_t pci_stub_read32(uint8_t b, uint8_t d, uint8_t f, uint8_t o) { (void)b;(void)d;(void)f;(void)o; return 0; }
static void pci_stub_write8 (uint8_t b, uint8_t d, uint8_t f, uint8_t o, uint8_t  v) { (void)b;(void)d;(void)f;(void)o;(void)v; }
static void pci_stub_write16(uint8_t b, uint8_t d, uint8_t f, uint8_t o, uint16_t v) { (void)b;(void)d;(void)f;(void)o;(void)v; }
static void pci_stub_write32(uint8_t b, uint8_t d, uint8_t f, uint8_t o, uint32_t v) { (void)b;(void)d;(void)f;(void)o;(void)v; }
#else

#define PCI_CFG_ADDR  0xCF8
#define PCI_CFG_DATA  0xCFC

static uintptr_t s_ecam_base = 0;  /* 0 = use legacy port on x86 */

void pci_set_ecam_base(uintptr_t phys_base) {
    s_ecam_base = phys_base;
}

static uint32_t pci_make_address(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    return 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (offset & 0xFC);
}

#if defined(__x86_64__) || defined(__i386__)
/* x86: port I/O for legacy config space (0xCF8=addr, 0xCFC=data) */
static uint32_t pci_port_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    uint32_t addr = pci_make_address(bus, dev, fn, offset);
    port_outl(PCI_CFG_ADDR, addr);
    return port_inl(PCI_CFG_DATA);
}
static void pci_port_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val) {
    uint32_t addr = pci_make_address(bus, dev, fn, offset);
    port_outl(PCI_CFG_ADDR, addr);
    port_outl(PCI_CFG_DATA, val);
}
#else
/* ARM: no port I/O - ECAM only */
static uint32_t pci_port_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    (void)bus;(void)dev;(void)fn;(void)offset;
    return 0xFFFFFFFFu;
}
static void pci_port_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val) {
    (void)bus;(void)dev;(void)fn;(void)offset;(void)val;
}
#endif

static uint32_t pci_read_config_raw(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    if (s_ecam_base != 0) {
        uintptr_t off = ((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) | ((uintptr_t)fn << 12) | (offset & 0xFFC);
        volatile uint32_t *cfg = (volatile uint32_t *)(s_ecam_base + off);
        return mmio_read32(cfg);
    }
#if defined(__x86_64__) || defined(__i386__)
    return pci_port_read32(bus, dev, fn, offset);
#else
    return 0xFFFFFFFFu;
#endif
}
static void pci_write_config_raw(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val) {
    if (s_ecam_base != 0) {
        uintptr_t off = ((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) | ((uintptr_t)fn << 12) | (offset & 0xFFC);
        volatile uint32_t *cfg = (volatile uint32_t *)(s_ecam_base + off);
        mmio_write32(cfg, val);
        return;
    }
#if defined(__x86_64__) || defined(__i386__)
    pci_port_write32(bus, dev, fn, offset, val);
#else
    (void)bus;(void)dev;(void)fn;(void)offset;(void)val;
#endif
}

#endif /* DRIVERS_BAREMETAL */

uint8_t pci_read_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
#ifndef DRIVERS_BAREMETAL
    return pci_stub_read8(bus, dev, fn, offset);
#else
    uint32_t v = pci_read_config_raw(bus, dev, fn, offset & 0xFFC);
    return (uint8_t)(v >> (8 * (offset & 3)));
#endif
}
uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
#ifndef DRIVERS_BAREMETAL
    return pci_stub_read16(bus, dev, fn, offset);
#else
    uint32_t v = pci_read_config_raw(bus, dev, fn, offset & 0xFFC);
    return (uint16_t)(v >> (8 * (offset & 2)));
#endif
}
uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
#ifndef DRIVERS_BAREMETAL
    return pci_stub_read32(bus, dev, fn, offset);
#else
    return pci_read_config_raw(bus, dev, fn, offset);
#endif
}
void pci_write_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint8_t v) {
#ifndef DRIVERS_BAREMETAL
    pci_stub_write8(bus, dev, fn, offset, v);
#else
    uint8_t ali = offset & 0xFFC;
    uint32_t old = pci_read_config_raw(bus, dev, fn, ali);
    uint32_t shift = 8 * (offset & 3);
    uint32_t mask = 0xFFu << shift;
    uint32_t nv = (old & ~mask) | ((uint32_t)v << shift);
    pci_write_config_raw(bus, dev, fn, ali, nv);
#endif
}
void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t v) {
#ifndef DRIVERS_BAREMETAL
    pci_stub_write16(bus, dev, fn, offset, v);
#else
    uint8_t ali = offset & 0xFFE;
    uint32_t old = pci_read_config_raw(bus, dev, fn, ali);
    uint32_t shift = 8 * (offset & 2);
    uint32_t mask = 0xFFFFu << shift;
    uint32_t nv = (old & ~mask) | ((uint32_t)v << shift);
    pci_write_config_raw(bus, dev, fn, ali, nv);
#endif
}
void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t v) {
#ifndef DRIVERS_BAREMETAL
    pci_stub_write32(bus, dev, fn, offset, v);
#else
    pci_write_config_raw(bus, dev, fn, offset, v);
#endif
}

uint32_t pci_get_caps(void) {
#ifdef DRIVERS_BAREMETAL
#if defined(__x86_64__) || defined(__i386__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
#else
    return FL_CAP_STUB;
#endif
}
