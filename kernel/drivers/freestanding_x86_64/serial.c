#include "serial.h"
#include "ioport.h"

#define COM1 0x3f8u

void fl_fs_serial_init(void)
{
    fl_fs_outb(COM1 + 1, 0);
    fl_fs_outb(COM1 + 3, 0x80);
    fl_fs_outb(COM1, 3);
    fl_fs_outb(COM1 + 1, 0);
    fl_fs_outb(COM1 + 3, 3);
    fl_fs_outb(COM1 + 2, 0xc7);
    fl_fs_outb(COM1 + 4, 0x0b);
}

void fl_fs_serial_putc(char c)
{
    while ((fl_fs_inb(COM1 + 5) & 0x20u) == 0) {
    }
    fl_fs_outb(COM1, (uint8_t)c);
}

void fl_fs_serial_puts(const char *s)
{
    while (*s)
        fl_fs_serial_putc(*s++);
}

int fl_fs_serial_getc(char *out)
{
    if ((fl_fs_inb(COM1 + 5) & 0x01u) == 0)
        return 0;
    *out = (char)fl_fs_inb(COM1);
    return 1;
}
