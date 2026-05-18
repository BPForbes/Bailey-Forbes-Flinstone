/**
 * **P4-3** PCIe lab helpers (ECAM/config, BAR decode, MSI setup).
 */
#ifndef FL_P4_PCIE_LAB_H
#define FL_P4_PCIE_LAB_H

#include <stdint.h>
#include "contract_result.h"
#include "fl/driver/pci.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t phys;
    uint64_t size;
    int is_mmio;
    int is_64bit;
} fl_pcie_bar_info_t;

/** Lab **QEMU virt**-class: DMA without programming IOMMU (documented assumption). */
int fl_pcie_lab_iommu_bypass_assumed(void);

fl_result_t fl_pcie_lab_read_config_dword(uint8_t bus, uint8_t dev, uint8_t fn,
                                          uint8_t offset, uint32_t *out);
fl_result_t fl_pcie_lab_write_config_dword(uint8_t bus, uint8_t dev, uint8_t fn,
                                           uint8_t offset, uint32_t value);

fl_result_t fl_pcie_lab_decode_bar(const pci_config_header_t *hdr, unsigned bar_index,
                                   fl_pcie_bar_info_t *out);

/** Program MSI capability (cap id 0x05) when present; separate from raw config peek/poke. */
fl_result_t fl_pcie_lab_setup_msi(uint8_t bus, uint8_t dev, uint8_t fn, unsigned vector);

#ifdef __cplusplus
}
#endif

#endif /* FL_P4_PCIE_LAB_H */
