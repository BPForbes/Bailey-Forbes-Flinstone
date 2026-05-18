/* xHCI MMIO helpers (P4-5) — AArch64 GAS
 *
 * AArch64 loads and stores for 32-bit xHCI registers plus **dsb sy** as a conservative
 * fence before doorbell writes after ring updates.
 *
 * SysV ABI: pointer in **x0**, 32-bit value in **w1** for writes; return **w0**.
 */
.section .note.GNU-stack,"",@progbits
.text
.globl fl_usb_xhci_mmio_read32_asm
.globl fl_usb_xhci_mmio_write32_asm
.globl fl_usb_xhci_mmio_rw_fence_asm

fl_usb_xhci_mmio_read32_asm:
    ldr     w0, [x0]
    ret

fl_usb_xhci_mmio_write32_asm:
    str     w1, [x0]
    ret

fl_usb_xhci_mmio_rw_fence_asm:
    dsb     sy
    ret
