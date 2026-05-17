; xHCI MMIO helpers (P4-5) — x86-64 NASM (SysV rdi, rsi, …)
section .note.GNU-stack progbits alloc noexec
section .text
global fl_usb_xhci_mmio_read32_asm
global fl_usb_xhci_mmio_write32_asm
global fl_usb_xhci_mmio_rw_fence_asm

fl_usb_xhci_mmio_read32_asm:
    mov eax, [rdi]
    ret

fl_usb_xhci_mmio_write32_asm:
    mov [rdi], esi
    ret

fl_usb_xhci_mmio_rw_fence_asm:
    mfence
    ret
