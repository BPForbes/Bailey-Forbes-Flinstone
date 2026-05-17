/**
 * xHCI MMIO glue (P4-5) — stable C entry points for controller register access.
 *
 * On supported architectures the implementation forwards to **fl_usb_xhci_mmio_*_asm**
 * in arch assembly; elsewhere it uses **fl_mmio_** helpers from **mmio.h** in the same
 * **fl/driver** include directory.
 */
#ifndef FL_DRIVER_USB_XHCI_MMIO_GLUE_H
#define FL_DRIVER_USB_XHCI_MMIO_GLUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t fl_usb_xhci_mmio_read32_volatile(volatile void *reg_ptr);
void fl_usb_xhci_mmio_write32_volatile(volatile void *reg_ptr, uint32_t value);
void fl_usb_xhci_mmio_rw_fence(void);

#ifdef __cplusplus
}
#endif

#endif /* FL_DRIVER_USB_XHCI_MMIO_GLUE_H */
