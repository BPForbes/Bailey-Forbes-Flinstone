#ifndef FL_FREESTANDING_VGA_H
#define FL_FREESTANDING_VGA_H

#include <stdint.h>

#define FL_FS_VGA_SHELL_ROWS 24
#define FL_FS_VGA_SHELL_COLS 80
#define FL_FS_VGA_SHELL_CELLS (FL_FS_VGA_SHELL_ROWS * FL_FS_VGA_SHELL_COLS)

void fl_fs_vga_init(void);
void fl_fs_vga_putc(char c);
void fl_fs_vga_puts(const char *s);
void fl_fs_vga_status(const char *user, unsigned session, unsigned sessions);
void fl_fs_vga_snapshot_shell(uint16_t *cells, unsigned count);
void fl_fs_vga_restore_shell(const uint16_t *cells, unsigned count);
void fl_fs_vga_clear_shell(void);
void fl_fs_vga_get_cursor(int *row, int *col);
void fl_fs_vga_set_cursor(int row, int col);

#endif
