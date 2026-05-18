/**
 * **P4-3 — PCIe config space access (lab)** (module contract, normative).
 *
 * **Distribution:** **ECAM** or platform **MMIO config** windows yield **DWORD-aligned**
 * config reads/writes; **BAR** decoding and **MSI/MSI-X** setup are **separate** obligations
 * from raw config peek/poke. **IOMMU** is out of scope for this row’s **lab** criterion;
 * document when DMA bypass is assumed (**QEMU virt** class lab).
 *
 * **Outcomes:** config and BAR mapping failures return **`fl_result_t`** (**P0-2**).
 */
#ifndef FL_CONTRACT_P4_PCIE_LAB_H
#define FL_CONTRACT_P4_PCIE_LAB_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_3_PCIE_LAB_CONTRACT_DEFINED 1

/** Lab **QEMU virt**-class targets may assume DMA without IOMMU programming. */
#define FL_CONTRACT_P4_3_LAB_IOMMU_BYPASS_ASSUMED 1

#endif /* FL_CONTRACT_P4_PCIE_LAB_H */
