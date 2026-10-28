#ifndef FL_HEX_H
#define FL_HEX_H

/**
 * The one hex codec. Header-only so every target (kernel, shell, tests, the
 * C++ password code) shares it without linking another object.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Value of one hex digit (either case), or -1. */
static inline int fl_hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/** Lower-case hex of in[0..len) into out, NUL-terminated; out holds 2 * len + 1 bytes. */
static inline void fl_hex_encode(const uint8_t *in, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < len; i++) {
        out[2 * i] = digits[in[i] >> 4];
        out[2 * i + 1] = digits[in[i] & 0x0f];
    }
    out[2 * len] = '\0';
}

/** 0 when hex is exactly 2 * len hex digits, written into out; otherwise -1 and out is unspecified. */
static inline int fl_hex_decode(const char *hex, uint8_t *out, size_t len) {
    size_t i;

    if (!hex || strlen(hex) != 2 * len)
        return -1;
    for (i = 0; i < len; i++) {
        int hi = fl_hex_nibble(hex[2 * i]);
        int lo = fl_hex_nibble(hex[2 * i + 1]);

        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
