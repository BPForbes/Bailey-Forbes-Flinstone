/* x86-64 Port I/O primitives - GAS/AT&T
 * Used by keyboard, timer, PIC, block when DRIVERS_BAREMETAL.
 */
.section .note.GNU-stack,"",@progbits
.text
.globl port_inb
.globl port_outb
.globl port_inw
.globl port_outw
.globl port_inl
.globl port_outl

/* uint8_t port_inb(uint16_t port) */
port_inb:
    movw %di, %dx
    xorl %eax, %eax
    inb %dx, %al
    ret

/* void port_outb(uint16_t port, uint8_t value) */
port_outb:
    movw %di, %dx
    movl %esi, %eax
    outb %al, %dx
    ret

/* uint16_t port_inw(uint16_t port) */
port_inw:
    movw %di, %dx
    xorl %eax, %eax
    inw %dx, %ax
    ret

/* void port_outw(uint16_t port, uint16_t value) */
port_outw:
    movw %di, %dx
    movl %esi, %eax
    outw %ax, %dx
    ret

/* uint32_t port_inl(uint16_t port) */
port_inl:
    movw %di, %dx
    xorl %eax, %eax
    inl %dx, %eax
    ret

/* void port_outl(uint16_t port, uint32_t value) */
port_outl:
    movw %di, %dx
    movl %esi, %eax
    outl %eax, %dx
    ret
