/*
 * WebAssembly / Emscripten host: C implementations of mem and port I/O that
 * are normally provided by x86-64 GAS objects. No real port I/O in the browser.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Copy n bytes from src to dst if n > 0 and both pointers are non-NULL.
 *
 * Copies exactly n bytes from the memory at src to dst. If n is zero or
 * either pointer is NULL, no operation is performed.
 *
 * @param dst Destination buffer to receive the copied bytes.
 * @param src Source buffer providing the bytes to copy.
 * @param n Number of bytes to copy.
 */
void asm_mem_copy(void *dst, const void *src, size_t n) {
    if (n && dst && src)
        memcpy(dst, src, n);
}

/**
 * Zero a memory region.
 *
 * Set `n` bytes starting at `ptr` to zero. If `ptr` is NULL or `n` is zero, no action is performed.
 *
 * @param ptr Pointer to the memory region to clear.
 * @param n Number of bytes to set to zero.
 */
void asm_mem_zero(void *ptr, size_t n) {
    if (n && ptr)
        memset(ptr, 0, n);
}

/**
 * Fill a memory region with a specified byte value.
 *
 * Sets each of the first `n` bytes starting at `ptr` to `byte`. If `ptr` is
 * NULL or `n` is zero, the function performs no action.
 *
 * @param ptr Pointer to the start of the memory region to fill.
 * @param byte Byte value to write into the memory region.
 * @param n Number of bytes to set.
void asm_block_fill(void *ptr, unsigned char byte, size_t n) {
    if (!ptr || !n)
        return;
    unsigned char *p = (unsigned char *)ptr;
    while (n--)
        *p++ = byte;
}

/**
 * Stub for an 8-bit port input operation on environments without real port I/O.
 *
 * This implementation ignores the provided port and always returns zero; it
 * serves as a no-op placeholder for WebAssembly/Emscripten hosts.
 *
 * @param port I/O port number (ignored).
 * @returns `0`.
 */
uint8_t port_inb(uint16_t port) {
    (void)port;
    return 0;
}

/**
 * Stub implementation for x86 outb port I/O; does nothing in the WebAssembly host.
 *
 * This function accepts a port number and an 8-bit value but intentionally ignores
 * both and performs no I/O or side effects. It exists to satisfy callers expecting
 * an outb symbol when running in environments (e.g., Emscripten/WebAssembly) where
 * real port I/O is unavailable.
 *
 * @param port  I/O port number (ignored).
 * @param value  8-bit value to write to the port (ignored).
 */
void port_outb(uint16_t port, uint8_t value) {
    (void)port;
    (void)value;
}

/**
 * Provide a 16-bit I/O port read stub for WebAssembly environments; does not perform hardware I/O.
 *
 * @param port I/O port number (ignored).
 * @returns `0` as the default stub value.
 */
uint16_t port_inw(uint16_t port) {
    (void)port;
    return 0;
}

/**
 * Provide a no-op stub for 16-bit port output on platforms without hardware port I/O.
 *
 * This function accepts a 16-bit port identifier and a 16-bit value but performs no action;
 * it exists so code that performs port output can link on environments (e.g., WebAssembly)
 * that do not support real port I/O.
 *
 * @param port Port identifier (ignored).
 * @param value Value to write to the port (ignored).
 */
void port_outw(uint16_t port, uint16_t value) {
    (void)port;
    (void)value;
}

/**
 * Read a 32-bit value from an I/O port. This is a WebAssembly/Emscripten stub that performs no actual port I/O.
 * @param port I/O port number (ignored).
 * @returns `0` as a 32-bit unsigned value.
 */
uint32_t port_inl(uint16_t port) {
    (void)port;
    return 0;
}

/**
 * No-op 32-bit port output stub used in WebAssembly/Emscripten builds.
 *
 * This function accepts a port number and a 32-bit value but intentionally
 * performs no I/O; it exists to satisfy interfaces that expect x86-style
 * port output functions when running in environments without real port access.
 *
 * @param port I/O port number (ignored).
 * @param value 32-bit value to write to the port (ignored).
 */
void port_outl(uint16_t port, uint32_t value) {
    (void)port;
    (void)value;
}
