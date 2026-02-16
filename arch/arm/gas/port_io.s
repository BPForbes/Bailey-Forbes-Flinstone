/* ARM port_io stubs - ARM has no x86-style port I/O (in/out instructions).
 * These stubs satisfy the linker when building for ARM. On bare-metal ARM,
 * drivers would use memory-mapped I/O instead. Host mode never calls these.
 */
.section .note.GNU-stack,"",@progbits
.text
.globl port_inb
.globl port_outb
.globl port_inw
.globl port_outw
.globl port_inl
.globl port_outl

/* uint8_t port_inb(uint16_t port) - return 0 */
port_inb:
    mov     w0, #0
    ret

/* void port_outb(uint16_t port, uint8_t value) - no-op */
port_outb:
    ret

/* uint16_t port_inw(uint16_t port) - return 0 */
port_inw:
    mov     w0, #0
    ret

/* void port_outw(uint16_t port, uint16_t value) - no-op */
port_outw:
    ret

/* uint32_t port_inl(uint16_t port) - return 0xFFFFFFFF (invalid) */
port_inl:
    mvn     w0, wzr
    ret

/* void port_outl(uint16_t port, uint32_t value) - no-op */
port_outl:
    ret
