/* Freestanding VGA putc: long lines wrap to the next row (#419). */
#include "vga.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define VGA_COLS 80
#define VGA_ROWS 25

static char cell_ch(const uint16_t *cells, int row, int col)
{
    return (char)(cells[(unsigned)(row - 1) * VGA_COLS + (unsigned)col] & 0xffu);
}

static int test_wrap_advances_row(void)
{
    uint16_t cells[FL_FS_VGA_SHELL_CELLS];
    int row = 0, col = 0;
    char line[VGA_COLS + 2];

    fl_fs_vga_init();
    fl_fs_vga_clear_shell();

    memset(line, 'A', VGA_COLS);
    line[VGA_COLS] = 'B';
    line[VGA_COLS + 1] = '\0';
    fl_fs_vga_puts(line);

    fl_fs_vga_get_cursor(&row, &col);
    ASSERT(row == 2);
    ASSERT(col == 1);

    fl_fs_vga_snapshot_shell(cells, FL_FS_VGA_SHELL_CELLS);
    ASSERT(cell_ch(cells, 1, 0) == 'A');
    ASSERT(cell_ch(cells, 1, VGA_COLS - 1) == 'A');
    ASSERT(cell_ch(cells, 2, 0) == 'B');
    ASSERT(cell_ch(cells, 2, 1) == ' ');
    return 0;
}

static int test_wrap_scrolls_at_bottom(void)
{
    uint16_t cells[FL_FS_VGA_SHELL_CELLS];
    int row = 0, col = 0;
    char full[VGA_COLS + 1];

    fl_fs_vga_init();
    fl_fs_vga_clear_shell();
    fl_fs_vga_set_cursor(VGA_ROWS - 1, 0);

    memset(full, 'X', VGA_COLS);
    full[VGA_COLS] = '\0';
    fl_fs_vga_puts(full);
    fl_fs_vga_putc('Y');

    fl_fs_vga_get_cursor(&row, &col);
    ASSERT(row == VGA_ROWS - 1);
    ASSERT(col == 1);

    fl_fs_vga_snapshot_shell(cells, FL_FS_VGA_SHELL_CELLS);
    ASSERT(cell_ch(cells, VGA_ROWS - 1, 0) == 'Y');
    ASSERT(cell_ch(cells, VGA_ROWS - 1, 1) == ' ');
    return 0;
}

static int test_newline_still_advances(void)
{
    int row = 0, col = 0;

    fl_fs_vga_init();
    fl_fs_vga_clear_shell();
    fl_fs_vga_puts("hi\n");
    fl_fs_vga_get_cursor(&row, &col);
    ASSERT(row == 2);
    ASSERT(col == 0);
    return 0;
}

int main(void)
{
    if (test_wrap_advances_row())
        return 1;
    if (test_wrap_scrolls_at_bottom())
        return 1;
    if (test_newline_still_advances())
        return 1;
    puts("test_fs_vga_wrap: PASS");
    return 0;
}
