/**
 * PCI Configuration Space - shared API for x86 and ARM.
 * x86: port 0xCF8/0xCFC (legacy) or ECAM (MMIO).
 * ARM: ECAM (MMIO) or stub. Same API on both arches.
 */
#ifndef FL_DRIVER_PCI_H
#define FL_DRIVER_PCI_H

#include <stdint.h>
#include "caps.h"

#define PCI_MAX_BUS  256
#define PCI_MAX_DEV  32
#define PCI_MAX_FN   8
#define PCI_CFG_SIZE 256

/* Packed structure for config header (first 64 bytes) */
typedef struct pci_config_header {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision_id;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  cache_line_size;
    uint8_t  latency_timer;
    uint8_t  header_type;
    uint8_t  bist;
    uint32_t bar[6];
    uint32_t cardbus_cis;
    uint16_t subsys_vendor;
    uint16_t subsys_id;
    uint32_t rom_base;
    uint8_t  caps_ptr;
    uint8_t  resvd1[3];
    uint32_t resvd2;
    uint8_t  irq_line;
    uint8_t  irq_pin;
    uint8_t  min_gnt;
    uint8_t  max_lat;
} __attribute__((packed)) pci_config_header_t;

/* Access by bus/dev/fn and register offset (0-255) */
uint8_t  pci_read_config8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset);
void     pci_write_config8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint8_t  v);
void     pci_write_config16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint16_t v);
void     pci_write_config32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t offset, uint32_t v);

/* Optional: set ECAM base (MMIO). If 0, x86 uses legacy port; ARM uses stub. */
void pci_set_ecam_base(uintptr_t phys_base);

/** Capability: FL_CAP_REAL if PCI works; FL_CAP_STUB if placeholder. Call before relying on enumeration. */
uint32_t pci_get_caps(void);

#endif /* FL_DRIVER_PCI_H */
