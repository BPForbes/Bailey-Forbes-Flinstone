#ifndef FL_FREESTANDING_SERIAL_H
#define FL_FREESTANDING_SERIAL_H

void fl_fs_serial_init(void);
void fl_fs_serial_putc(char c);
void fl_fs_serial_puts(const char *s);
int fl_fs_serial_getc(char *out);

#endif
