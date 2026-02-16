/* x86-64 Port I/O primitives - ASM layer for hardware drivers
 * Used by keyboard, timer, PIC, block (IDE) when DRIVERS_BAREMETAL.
 * In userspace these require ioperm() on Linux; on bare-metal they work directly.
 */
.text
.globl port_inb
.globl port_outb
.globl port_inw
.globl port_outw

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
