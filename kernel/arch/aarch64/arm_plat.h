/**
 * ARM platform MMIO addresses (QEMU virt, versatilepb).
 */
#ifndef ARM_PLAT_H
#define ARM_PLAT_H

#include <stdint.h>

#define ARM_PL011_UART_BASE  0x09000000
#define ARM_GICD_BASE       0x08000000
#define ARM_GICC_BASE       0x08010000
#define ARM_PCI_ECAM_DEFAULT 0x4010000000ULL

void arm_plat_init(uintptr_t pci_ecam_base);
uintptr_t arm_plat_get_ecam_base(void);

#endif /* ARM_PLAT_H */
