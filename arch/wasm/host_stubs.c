/*
 * WebAssembly / Emscripten host: C implementations of mem and port I/O that
 * are normally provided by x86-64 GAS objects. No real port I/O in the browser.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void asm_mem_copy(void *dst, const void *src, size_t n) {
    if (n && dst && src)
        memcpy(dst, src, n);
}

void asm_mem_zero(void *ptr, size_t n) {
    if (n && ptr)
        memset(ptr, 0, n);
}

void asm_block_fill(void *ptr, unsigned char byte, size_t n) {
    if (!ptr || !n)
        return;
    unsigned char *p = (unsigned char *)ptr;
    while (n--)
        *p++ = byte;
}

uint8_t port_inb(uint16_t port) {
    (void)port;
    return 0;
}

void port_outb(uint16_t port, uint8_t value) {
    (void)port;
    (void)value;
}

uint16_t port_inw(uint16_t port) {
    (void)port;
    return 0;
}

void port_outw(uint16_t port, uint16_t value) {
    (void)port;
    (void)value;
}

uint32_t port_inl(uint16_t port) {
    (void)port;
    return 0;
}

void port_outl(uint16_t port, uint32_t value) {
    (void)port;
    (void)value;
}
