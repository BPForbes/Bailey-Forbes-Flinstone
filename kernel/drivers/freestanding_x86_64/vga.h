#ifndef FL_FREESTANDING_VGA_H
#define FL_FREESTANDING_VGA_H

void fl_fs_vga_init(void);
void fl_fs_vga_putc(char c);
void fl_fs_vga_puts(const char *s);
void fl_fs_vga_status(const char *user, unsigned session, unsigned sessions);

#endif
