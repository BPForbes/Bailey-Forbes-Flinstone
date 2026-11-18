#ifndef FL_FREESTANDING_VGA_SGR_H
#define FL_FREESTANDING_VGA_SGR_H

#include <stdint.h>

/* ANSI SGR colour for the VGA console; supported codes: tools/browser-lab/README.md. */

#define FL_VGA_SGR_DEFAULT_ATTR 0x07u
#define FL_VGA_SGR_MAX_PARAMS 8u

struct fl_vga_sgr {
    uint8_t state;
    uint8_t attr;
    uint8_t bad;
    uint8_t nparams;
    uint16_t cur;
    uint16_t params[FL_VGA_SGR_MAX_PARAMS];
};

static inline void fl_vga_sgr_reset(struct fl_vga_sgr *s)
{
    s->state = 0;
    s->attr = FL_VGA_SGR_DEFAULT_ATTR;
    s->bad = 0;
    s->nparams = 0;
    s->cur = 0;
}

static inline void fl_vga_sgr_push(struct fl_vga_sgr *s)
{
    if (s->nparams < FL_VGA_SGR_MAX_PARAMS)
        s->params[s->nparams++] = s->cur;
    else
        s->bad = 1;
    s->cur = 0;
}

static inline void fl_vga_sgr_apply(struct fl_vga_sgr *s)
{
    static const uint8_t ansi_to_vga[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    uint8_t fg = (uint8_t)(s->attr & 0x0fu);
    uint8_t bg = (uint8_t)((s->attr >> 4) & 0x07u);
    for (unsigned i = 0; i < s->nparams; ++i) {
        unsigned p = s->params[i];
        if (p == 0) {
            fg = FL_VGA_SGR_DEFAULT_ATTR & 0x0fu;
            bg = 0;
        } else if (p == 1) {
            fg |= 0x08u;
        } else if (p == 22) {
            fg &= 0x07u;
        } else if (p >= 30 && p <= 37) {
            fg = (uint8_t)((fg & 0x08u) | ansi_to_vga[p - 30]);
        } else if (p == 39) {
            fg = (uint8_t)((fg & 0x08u) | 0x07u);
        } else if (p >= 40 && p <= 47) {
            bg = ansi_to_vga[p - 40];
        } else if (p == 49) {
            bg = 0;
        } else if (p >= 90 && p <= 97) {
            fg = (uint8_t)(0x08u | ansi_to_vga[p - 90]);
        } else if (p >= 100 && p <= 107) {
            /* Attribute bit 7 blinks on VGA, so bright backgrounds use the normal colour. */
            bg = ansi_to_vga[p - 100];
        } else if (p == 38 || p == 48) {
            break;
        }
    }
    s->attr = (uint8_t)((bg << 4) | fg);
}

/* Returns 1 when `c` is part of an escape sequence and must not be drawn. */
static inline int fl_vga_sgr_feed(struct fl_vga_sgr *s, char c)
{
    unsigned char u = (unsigned char)c;
    if (s->state == 0) {
        if (u != 0x1bu)
            return 0;
        s->state = 1;
        return 1;
    }
    if (u < 0x20u) {
        s->state = 0;
        return 0;
    }
    if (s->state == 1) {
        if (u == '[') {
            s->state = 2;
            s->bad = 0;
            s->nparams = 0;
            s->cur = 0;
        } else {
            s->state = 0;
        }
        return 1;
    }
    if (u >= '0' && u <= '9') {
        if (s->cur < 1000u)
            s->cur = (uint16_t)(s->cur * 10u + (u - '0'));
        return 1;
    }
    if (u == ';') {
        fl_vga_sgr_push(s);
        return 1;
    }
    if (u >= 0x40u && u <= 0x7eu) {
        s->state = 0;
        if (u == 'm' && !s->bad) {
            fl_vga_sgr_push(s);
            if (!s->bad)
                fl_vga_sgr_apply(s);
        }
        return 1;
    }
    s->bad = 1;
    return 1;
}

#endif
