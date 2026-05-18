/**
 * xHCI MMIO primitives (P4-5) — architecture-backed **32-bit** register access.
 *
 * Implemented in **usb_xhci_mmio_asm** translation units: GAS under **kernel/arch** for
 * AArch64 and x86_64, NASM under **arch/x86_64/nasm** when **ARCH=x86_64_nasm**. C callers
 * should prefer **fl_usb_xhci_mmio_*_volatile** in **usb_xhci_mmio_glue.h** so hosted
 * fallbacks stay centralized.
 */
#ifndef FL_DRIVER_USB_XHCI_MMIO_ASM_H
#define FL_DRIVER_USB_XHCI_MMIO_ASM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t fl_usb_xhci_mmio_read32_asm(void *reg_ptr);
void fl_usb_xhci_mmio_write32_asm(void *reg_ptr, uint32_t value);
void fl_usb_xhci_mmio_rw_fence_asm(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_USB_XHCI_MMIO_ASM_H */
