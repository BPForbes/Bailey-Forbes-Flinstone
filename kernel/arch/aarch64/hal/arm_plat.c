/**
 * ARM platform configuration.
 */
#include "arm_plat.h"

static uintptr_t s_ecam_base = ARM_PCI_ECAM_DEFAULT;

void arm_plat_init(uintptr_t pci_ecam_base) {
    if (pci_ecam_base != 0)
        s_ecam_base = pci_ecam_base;
}

uintptr_t arm_plat_get_ecam_base(void) {
    return s_ecam_base;
}
