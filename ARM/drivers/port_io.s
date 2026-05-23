/* ARM64 Port I/O - Note: ARM doesn't have port I/O like x86.
 * This is a stub implementation that may need platform-specific handling.
 * For bare-metal ARM, you might need MMIO or platform-specific I/O functions.
 */
.text
.globl port_inb
.globl port_outb
.globl port_inw
.globl port_outw

/* uint8_t port_inb(uint16_t port) */
/* x0 = port */
port_inb:
    /* ARM doesn't have port I/O - this is a stub */
    /* For real implementation, use MMIO or platform-specific code */
    mov w0, #0          /* return 0 as stub */
    ret

/* void port_outb(uint16_t port, uint8_t value) */
/* x0 = port, x1 = value */
port_outb:
    /* ARM doesn't have port I/O - this is a stub */
    /* For real implementation, use MMIO or platform-specific code */
    ret

/* uint16_t port_inw(uint16_t port) */
/* x0 = port */
port_inw:
    /* ARM doesn't have port I/O - this is a stub */
    mov w0, #0          /* return 0 as stub */
    ret

/* void port_outw(uint16_t port, uint16_t value) */
/* x0 = port, x1 = value */
port_outw:
    /* ARM doesn't have port I/O - this is a stub */
    ret
