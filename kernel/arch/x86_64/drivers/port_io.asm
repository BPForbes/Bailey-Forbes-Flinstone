; x86-64 Port I/O primitives - ASM layer for hardware drivers (NASM)
; Used by keyboard, timer, PIC, block (IDE) when DRIVERS_BAREMETAL.
; In userspace these require ioperm() on Linux; on bare-metal they work directly.
section .text

global port_inb
global port_outb
global port_inw
global port_outw

; uint8_t port_inb(uint16_t port)
; di = port
port_inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

; void port_outb(uint16_t port, uint8_t value)
; di = port, si = value
port_outb:
    mov dx, di
    mov eax, esi
    out dx, al
    ret

; uint16_t port_inw(uint16_t port)
; di = port
port_inw:
    mov dx, di
    xor eax, eax
    in ax, dx
    ret

; void port_outw(uint16_t port, uint16_t value)
; di = port, si = value
port_outw:
    mov dx, di
    mov eax, esi
    out dx, ax
    ret
