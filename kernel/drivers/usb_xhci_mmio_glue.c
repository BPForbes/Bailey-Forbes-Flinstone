#include "fl/driver/mmio.h"
#include "fl/driver/usb_xhci_mmio_asm.h"
#include "fl/driver/usb_xhci_mmio_glue.h"
#include <stddef.h>
#include <stdint.h>

#if defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64)
#define FL_USB_XHCI_MMIO_HAS_ARCH_ASM 1
#else
#define FL_USB_XHCI_MMIO_HAS_ARCH_ASM 0
#endif

uint32_t fl_usb_xhci_mmio_read32_volatile(volatile void *reg_ptr) {
#if FL_USB_XHCI_MMIO_HAS_ARCH_ASM
    return fl_usb_xhci_mmio_read32_asm((void *)(uintptr_t)reg_ptr);
#else
    return fl_mmio_read32(reg_ptr);
#endif
}

void fl_usb_xhci_mmio_write32_volatile(volatile void *reg_ptr, uint32_t value) {
#if FL_USB_XHCI_MMIO_HAS_ARCH_ASM
    fl_usb_xhci_mmio_write32_asm((void *)(uintptr_t)reg_ptr, value);
#else
    fl_mmio_write32(reg_ptr, value);
#endif
}

void fl_usb_xhci_mmio_rw_fence(void) {
#if FL_USB_XHCI_MMIO_HAS_ARCH_ASM
    fl_usb_xhci_mmio_rw_fence_asm();
#else
    __asm__ volatile("" ::: "memory");
#endif
}
