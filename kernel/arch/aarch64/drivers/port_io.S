/* ARM64 Port I/O stubs - ARM has no x86-style in/out instructions.
 * These satisfy the fl_ioport HAL interface and return the standard
 * "no device present" sentinel values used throughout the driver layer:
 *   port_inb  -> 0xFF        (1-byte no-device)
 *   port_inw  -> 0xFFFF      (2-byte no-device)
 *   port_inl  -> 0xFFFFFFFF  (4-byte no-device)
 *   port_out* -> no-op
 * On bare-metal ARM, actual peripheral I/O uses MMIO via fl_mmio_*.
 */
.section .note.GNU-stack,"",%progbits
.text
.globl port_inb
.globl port_outb
.globl port_inw
.globl port_outw
.globl port_inl
.globl port_outl

/* uint8_t port_inb(uint16_t port) — return 0xFF */
/* x0 = port */
port_inb:
    mov     w0, #0xFF
    ret

/* void port_outb(uint16_t port, uint8_t value) — no-op */
/* x0 = port, x1 = value */
port_outb:
    ret

/* uint16_t port_inw(uint16_t port) — return 0xFFFF */
/* x0 = port */
port_inw:
    movz    w0, #0xffff
    ret

/* void port_outw(uint16_t port, uint16_t value) — no-op */
/* x0 = port, x1 = value */
port_outw:
    ret

/* uint32_t port_inl(uint16_t port) — return 0xFFFFFFFF */
/* x0 = port */
port_inl:
    mvn     w0, wzr
    ret

/* void port_outl(uint16_t port, uint32_t value) — no-op */
/* x0 = port, x1 = value */
port_outl:
    ret