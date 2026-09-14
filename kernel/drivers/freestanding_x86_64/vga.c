#include "vga.h"
#include <stdint.h>

#define VGA_COLS 80
#define VGA_ROWS 25
#define DIAG_GLYPH ((uint16_t)(0x0700u | 'F'))

static volatile uint16_t *const s_vga = (volatile uint16_t *)(uintptr_t)0xb8000;
static int s_row = 1;
static int s_col = 0;

static void put_cell(int row, int col, char ch, uint8_t attr)
{
    s_vga[row * VGA_COLS + col] = (uint16_t)(((uint16_t)attr << 8) | (uint8_t)ch);
}

static void clear_row(int row, uint8_t attr)
{
    for (int col = 0; col < VGA_COLS; ++col)
        put_cell(row, col, ' ', attr);
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
    char line[VGA_COLS];
    int n = 0;
    const char *prefix = " flintstone  user=";
    s_vga[0] = DIAG_GLYPH;
    while (prefix[n] && n < VGA_COLS - 1) {
        line[n] = prefix[n];
        ++n;
    }
    while (*user && n < VGA_COLS - 1)
        line[n++] = *user++;
    const char *mid = "  sess ";
    while (*mid && n < VGA_COLS - 1)
        line[n++] = *mid++;
    if (session >= 10 && n < VGA_COLS - 1)
        line[n++] = (char)('0' + (session / 10));
    if (n < VGA_COLS - 1)
        line[n++] = (char)('0' + (session % 10));
    if (n < VGA_COLS - 1)
        line[n++] = '/';
    if (n < VGA_COLS - 1)
        line[n++] = (char)('0' + (sessions % 10));
    while (n < VGA_COLS - 1)
        line[n++] = ' ';
    for (int col = 1; col < VGA_COLS; ++col)
        put_cell(0, col, line[col - 1], 0x0e);
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
        if (s_row >= VGA_ROWS) {
            for (int row = 1; row < VGA_ROWS - 1; ++row) {
                for (int col = 0; col < VGA_COLS; ++col)
                    s_vga[row * VGA_COLS + col] = s_vga[(row + 1) * VGA_COLS + col];
            }
            clear_row(VGA_ROWS - 1, 0x07);
            s_row = VGA_ROWS - 1;
        }
        if (c == '\n')
            return;
    }
    if (c == '\b') {
        if (s_col > 0) {
            --s_col;
            put_cell(s_row, s_col, ' ', 0x07);
        }
        return;
    }
    put_cell(s_row, s_col, c, 0x07);
    ++s_col;
}

void fl_fs_vga_puts(const char *s)
{
    while (*s)
        fl_fs_vga_putc(*s++);
}
