; x86-64 Port I/O primitives - NASM
section .note.GNU-stack progbits alloc noexec
section .text
global port_inb
global port_outb
global port_inw
global port_outw
global port_inl
global port_outl

; uint8_t port_inb(uint16_t port)
port_inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

; void port_outb(uint16_t port, uint8_t value)
port_outb:
    mov dx, di
    mov eax, esi
    out dx, al
    ret

; uint16_t port_inw(uint16_t port)
port_inw:
    mov dx, di
    xor eax, eax
    in ax, dx
    ret

; void port_outw(uint16_t port, uint16_t value)
port_outw:
    mov dx, di
    mov eax, esi
    out dx, ax
    ret

; uint32_t port_inl(uint16_t port)
port_inl:
    mov dx, di
    xor eax, eax
    in eax, dx
    ret

; void port_outl(uint16_t port, uint32_t value)
port_outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret
