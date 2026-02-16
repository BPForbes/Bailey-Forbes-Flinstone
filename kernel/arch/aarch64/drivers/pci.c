/**
 * AArch64 PCI via ECAM (MMIO). Real implementation.
 * Requires pci_set_ecam_base() from DT/bootloader; default for QEMU virt.
 */
#include "fl/driver/pci.h"
#include "fl/driver/mmio.h"
#include "arm_plat.h"
#include <stddef.h>

static uintptr_t s_ecam_base = 0;

void pci_set_ecam_base(uintptr_t phys_base) {
    s_ecam_base = phys_base;
}

static uint32_t pci_ecam_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    if (s_ecam_base == 0)
        s_ecam_base = arm_plat_get_ecam_base();
    if (s_ecam_base == 0)
        return 0xFFFFFFFFu;
    uintptr_t off = ((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) |
                    ((uintptr_t)fn << 12) | (offset & 0xFFC);
    volatile uint32_t *cfg = (volatile uint32_t *)(s_ecam_base + off);
    return fl_mmio_read32((void *)cfg);
}

static void pci_ecam_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t val) {
    if (s_ecam_base == 0)
        s_ecam_base = arm_plat_get_ecam_base();
    if (s_ecam_base == 0)
        return;
    uintptr_t off = ((uintptr_t)bus << 20) | ((uintptr_t)dev << 15) |
                    ((uintptr_t)fn << 12) | (offset & 0xFFC);
    volatile uint32_t *cfg = (volatile uint32_t *)(s_ecam_base + off);
    fl_mmio_write32((void *)cfg, val);
}

uint32_t pci_get_caps(void) {
    if (s_ecam_base == 0)
        s_ecam_base = arm_plat_get_ecam_base();
    return (s_ecam_base != 0) ? FL_CAP_REAL : FL_CAP_STUB;
}

uint8_t pci_read_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    uint32_t v = pci_ecam_read(bus, dev, fn, offset & 0xFFC);
    return (uint8_t)(v >> (8 * (offset & 3)));
}

uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    uint32_t v = pci_ecam_read(bus, dev, fn, offset & 0xFFC);
    return (uint16_t)(v >> (8 * (offset & 2)));
}

uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    return pci_ecam_read(bus, dev, fn, offset);
}

void pci_write_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint8_t v) {
    uint32_t old = pci_ecam_read(bus, dev, fn, offset & 0xFFC);
    uint32_t shift = 8 * (offset & 3);
    uint32_t mask = 0xFFu << shift;
    uint32_t nv = (old & ~mask) | ((uint32_t)v << shift);
    pci_ecam_write(bus, dev, fn, offset & 0xFFC, nv);
}

void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t v) {
    uint32_t old = pci_ecam_read(bus, dev, fn, offset & 0xFFC);
    uint32_t shift = 8 * (offset & 2);
    uint32_t mask = 0xFFFFu << shift;
    uint32_t nv = (old & ~mask) | ((uint32_t)v << shift);
    pci_ecam_write(bus, dev, fn, offset & 0xFFC, nv);
}

void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t v) {
    pci_ecam_write(bus, dev, fn, offset, v);
}
