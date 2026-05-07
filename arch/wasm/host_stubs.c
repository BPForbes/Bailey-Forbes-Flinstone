/*
 * WebAssembly / Emscripten host: C implementations of mem and port I/O that
 * are normally provided by x86-64 GAS objects. No real port I/O in the browser.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* memcpy-sized copy; NULL or zero length is a no-op. */
void asm_mem_copy(void *dst, const void *src, size_t n) {
    if (n && dst && src)
        memcpy(dst, src, n);
}

/* memset to zero; NULL or zero length is a no-op. */
void asm_mem_zero(void *ptr, size_t n) {
    if (n && ptr)
        memset(ptr, 0, n);
}

/* Fill n bytes with byte; no-op when ptr is NULL or n is zero. */
void asm_block_fill(void *ptr, unsigned char byte, size_t n) {
    if (!ptr || !n)
        return;
    unsigned char *p = (unsigned char *)ptr;
    while (n--)
        *p++ = byte;
}

/* x86-style I/O stubs: browser has no real ports; always read zero / ignore writes. */
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
