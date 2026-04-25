/**
 * Unified keyboard driver - uses fl_ioport for baremetal.
 * Host: stdin. BAREMETAL: PS/2 port I/O via HAL.
 */
#include "drivers.h"
#include "fl/driver/console.h"
#include "fl/driver/ioport.h"
#include "fl/driver/caps.h"
#include "fl/driver/driver_types.h"
#include "fl/mm.h"
#include "fl/mem_asm.h"
#ifndef DRIVERS_BAREMETAL
#include <unistd.h>
#endif

#define KB_DATA   0x60
#define KB_STATUS 0x64

typedef struct {
    keyboard_driver_t base;
    int host_mode;
} keyboard_impl_t;

#ifndef DRIVERS_BAREMETAL
static int host_poll_scancode(keyboard_driver_t *drv, uint8_t *out) {
    (void)drv;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        *out = (uint8_t)c;
        return 0;
    }
    return -1;
}

static int host_get_char(keyboard_driver_t *drv, char *out) {
    uint8_t sc;
    if (host_poll_scancode(drv, &sc) == 0) {
        *out = (char)sc;
        return 0;
    }
    return -1;
}
#endif

#ifdef DRIVERS_BAREMETAL
#if defined(__x86_64__) || defined(__i386__)

/*
 * PS/2 Set-1 make-code to ASCII translation tables.
 * Index = scan code (0x00–0x58).  0 = non-printable / unhandled.
 */
static const char s_sc_unshifted[89] = {
/*00*/  0,    0,   '1', '2', '3', '4', '5', '6',
/*08*/ '7',  '8', '9', '0', '-', '=',  0,    0,
/*10*/ 'q',  'w', 'e', 'r', 't', 'y', 'u',  'i',
/*18*/ 'o',  'p', '[', ']', '\n', 0,  'a',  's',
/*20*/ 'd',  'f', 'g', 'h', 'j', 'k', 'l',  ';',
/*28*/ '\'', '`',  0,  '\\','z', 'x', 'c',  'v',
/*30*/ 'b',  'n', 'm', ',', '.', '/', 0,    '*',
/*38*/  0,   ' ',  0,   0,   0,   0,   0,    0,
/*40*/  0,    0,   0,   0,   0,   0,   0,   '7',
/*48*/ '8',  '9', '-', '4', '5', '6', '+',  '1',
/*50*/ '2',  '3', '0', '.'
};

static const char s_sc_shifted[89] = {
/*00*/  0,    0,   '!', '@', '#', '$', '%', '^',
/*08*/ '&',  '*', '(', ')', '_', '+',  0,    0,
/*10*/ 'Q',  'W', 'E', 'R', 'T', 'Y', 'U',  'I',
/*18*/ 'O',  'P', '{', '}', '\n', 0,  'A',  'S',
/*20*/ 'D',  'F', 'G', 'H', 'J', 'K', 'L',  ':',
/*28*/ '"',  '~',  0,  '|', 'Z', 'X', 'C',  'V',
/*30*/ 'B',  'N', 'M', '<', '>', '?', 0,    '*',
/*38*/  0,   ' ',  0,   0,   0,   0,   0,    0,
/*40*/  0,    0,   0,   0,   0,   0,   0,   '7',
/*48*/ '8',  '9', '-', '4', '5', '6', '+',  '1',
/*50*/ '2',  '3', '0', '.'
};

/* Modifier scan codes (Set 1) */
#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36
#define SC_CAPSLOCK 0x3A
#define SC_BREAK   0x80   /* bit 7 set = key release */

static int s_shift    = 0;
static int s_capslock = 0;

static int hw_poll_scancode(keyboard_driver_t *drv, uint8_t *out) {
    (void)drv;
    if ((fl_ioport_in8(KB_STATUS) & 0x01) == 0)
        return -1;
    *out = (uint8_t)fl_ioport_in8(KB_DATA);
    return 0;
}

static int hw_get_char(keyboard_driver_t *drv, char *out) {
    uint8_t sc;
    if (hw_poll_scancode(drv, &sc) != 0)
        return -1;

    /* Break code: bit 7 set — update modifier state, emit nothing */
    if (sc & SC_BREAK) {
        uint8_t make = sc & ~SC_BREAK;
        if (make == SC_LSHIFT || make == SC_RSHIFT)
            s_shift = 0;
        return -1;
    }

    /* Make codes for modifiers */
    if (sc == SC_LSHIFT || sc == SC_RSHIFT) { s_shift = 1; return -1; }
    if (sc == SC_CAPSLOCK) { s_capslock ^= 1; return -1; }

    /* Translate printable make codes */
    if (sc >= (uint8_t)(sizeof(s_sc_unshifted) / sizeof(s_sc_unshifted[0])))
        return -1;
    int use_upper = s_shift ^ s_capslock;
    char c = use_upper ? s_sc_shifted[sc] : s_sc_unshifted[sc];
    if (!c)
        return -1;
    *out = c;
    return 0;
}
#elif defined(__aarch64__)
#include "hal/arm_uart.h"
static int hw_poll_scancode(keyboard_driver_t *drv, uint8_t *out) {
    (void)drv;
    return arm_uart_poll(out);
}

static int hw_get_char(keyboard_driver_t *drv, char *out) {
    (void)drv;
    return arm_uart_getchar(out);
}
#endif
#endif

keyboard_driver_t *keyboard_driver_create(void) {
    keyboard_impl_t *impl = (keyboard_impl_t *)kmalloc(sizeof(*impl));
    if (impl) asm_mem_zero(impl, sizeof(*impl));
    if (!impl) return NULL;
#ifdef DRIVERS_BAREMETAL
#if defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
    impl->host_mode = 0;
    impl->base.poll_scancode = hw_poll_scancode;
    impl->base.get_char = hw_get_char;
#else
    /* Bare-metal build on unsupported architecture: no keyboard support.
     * host_poll_scancode/host_get_char are not available in DRIVERS_BAREMETAL.
     * Set to NULL to avoid undefined references; caller must check caps. */
    impl->host_mode = 0;
    impl->base.poll_scancode = NULL;
    impl->base.get_char = NULL;
#endif
#else
    impl->host_mode = 1;
    impl->base.poll_scancode = host_poll_scancode;
    impl->base.get_char = host_get_char;
#endif
    impl->base.impl = impl;
    return &impl->base;
}

void keyboard_driver_destroy(keyboard_driver_t *drv) {
    kfree(drv);
}

uint32_t keyboard_driver_caps(void) {
    if (!g_keyboard_driver) return 0;
#ifndef DRIVERS_BAREMETAL
    return FL_CAP_REAL;
#elif defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
    return FL_CAP_REAL;
#else
    return FL_CAP_STUB;
#endif
}