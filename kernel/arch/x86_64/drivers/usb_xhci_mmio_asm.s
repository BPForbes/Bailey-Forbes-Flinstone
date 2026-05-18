/* xHCI MMIO helpers (P4-5) — x86-64 GAS
 *
 * Single plain loads and stores for 32-bit xHCI register windows. **mfence** in the
 * fence entry point gives a conservative **host-memory ↔ MMIO** ordering barrier for
 * ring-pointer publication before doorbell writes (see **Intel xHCI** §4).
 *
 * SysV AMD64 ABI: first pointer in **rdi**, 32-bit value in **esi** for writes.
 */
.section .note.GNU-stack,"",@progbits
.text
.globl fl_usb_xhci_mmio_read32_asm
.globl fl_usb_xhci_mmio_write32_asm
.globl fl_usb_xhci_mmio_rw_fence_asm

fl_usb_xhci_mmio_read32_asm:
    movl    (%rdi), %eax
    ret

fl_usb_xhci_mmio_write32_asm:
    movl    %esi, (%rdi)
    ret

fl_usb_xhci_mmio_rw_fence_asm:
    mfence
    ret
