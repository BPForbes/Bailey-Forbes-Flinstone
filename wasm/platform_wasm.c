#include "keyboard.h"
#include "serial.h"
#include "vga.h"
#include <stdint.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE
#endif

#define VGA_COLS 80
#define VGA_ROWS 25
#define DIAG_GLYPH ((uint16_t)(0x0700u | 'F'))

static uint16_t s_vga[VGA_COLS * VGA_ROWS];
static int s_row = 1;
static int s_col = 0;

static void emit_js(char c)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof Module.onShellChar === "function")
            Module.onShellChar($0);
    }, (int)(unsigned char)c);
#else
    extern int putchar(int);
    putchar((unsigned char)c);
#endif
}

void fl_fs_serial_init(void) {}

void fl_fs_serial_putc(char c)
{
    emit_js(c);
}

void fl_fs_serial_puts(const char *s)
{
    if (!s)
        return;
    while (*s)
        fl_fs_serial_putc(*s++);
}

int fl_fs_serial_getc(char *out)
{
    (void)out;
    return 0;
}

static void put_cell(int row, int col, char ch, uint8_t attr)
{
    s_vga[row * VGA_COLS + col] = (uint16_t)(((uint16_t)attr << 8) | (uint8_t)ch);
}

void fl_fs_vga_init(void)
{
    for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        s_vga[i] = (uint16_t)(0x0700u | ' ');
    s_vga[0] = DIAG_GLYPH;
    s_row = 1;
    s_col = 0;
    fl_fs_vga_status("flinstone", 1, 1);
}

void fl_fs_vga_status(const char *user, unsigned session, unsigned sessions)
{
    (void)user;
    (void)session;
    (void)sessions;
    s_vga[0] = DIAG_GLYPH;
}

void fl_fs_vga_putc(char c)
{
    if (c == '\r') {
        s_col = 0;
        return;
    }
    if (c == '\n' || s_col >= VGA_COLS) {
        s_col = 0;
        if (c == '\n')
            ++s_row;
        if (s_row >= VGA_ROWS)
            s_row = VGA_ROWS - 1;
        if (c == '\n')
            return;
    }
    if (c == '\b') {
        if (s_col > 0)
            --s_col;
        return;
    }
    put_cell(s_row, s_col++, c, 0x07);
}

void fl_fs_vga_puts(const char *s)
{
    if (!s)
        return;
    while (*s)
        fl_fs_vga_putc(*s++);
}

void fl_fs_vga_snapshot_shell(uint16_t *cells, unsigned count)
{
    unsigned n = count;
    if (n > FL_FS_VGA_SHELL_CELLS)
        n = FL_FS_VGA_SHELL_CELLS;
    for (unsigned i = 0; i < n; ++i)
        cells[i] = s_vga[VGA_COLS + i];
}

void fl_fs_vga_restore_shell(const uint16_t *cells, unsigned count)
{
    unsigned n = count;
    if (n > FL_FS_VGA_SHELL_CELLS)
        n = FL_FS_VGA_SHELL_CELLS;
    for (unsigned i = 0; i < n; ++i)
        s_vga[VGA_COLS + i] = cells[i];
}

void fl_fs_vga_clear_shell(void)
{
    for (int i = VGA_COLS; i < VGA_COLS * VGA_ROWS; ++i)
        s_vga[i] = (uint16_t)(0x0700u | ' ');
    s_row = 1;
    s_col = 0;
}

void fl_fs_vga_get_cursor(int *row, int *col)
{
    if (row)
        *row = s_row;
    if (col)
        *col = s_col;
}

void fl_fs_vga_set_cursor(int row, int col)
{
    s_row = row;
    s_col = col;
}

void fl_fs_kbd_init(void) {}

void fl_fs_kbd_irq(void) {}

int fl_fs_kbd_getc(char *out)
{
    (void)out;
    return 0;
}

EMSCRIPTEN_KEEPALIVE uint16_t *wasm_vga_buffer(void)
{
    return s_vga;
}

EMSCRIPTEN_KEEPALIVE int wasm_vga_bytes(void)
{
    return VGA_COLS * VGA_ROWS * (int)sizeof(uint16_t);
}
