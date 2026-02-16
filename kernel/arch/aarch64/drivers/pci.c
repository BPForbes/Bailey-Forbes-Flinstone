/**
 * AArch64 PCI stub - same API as x86, no hardware access.
 * ARM typically uses ECAM (MMIO); this stub allows wiring parity.
 * Future: implement ECAM when base is set via pci_set_ecam_base().
 */
#include "fl/driver/pci.h"
#include <stddef.h>

void pci_set_ecam_base(uintptr_t phys_base) {
    (void)phys_base;
}

uint8_t pci_read_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    return 0xFF;
}

uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    return 0xFFFF;
}

uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    return 0xFFFFFFFFu;
}

void pci_write_config8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint8_t v) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    (void)v;
}

void pci_write_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t v) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    (void)v;
}

void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t v) {
    (void)bus;
    (void)dev;
    (void)fn;
    (void)offset;
    (void)v;
}
