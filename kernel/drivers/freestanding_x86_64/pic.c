#include "pic.h"
#include "ioport.h"

#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xa0
#define PIC2_DATA 0xa1

void fl_fs_pic_init(void)
{
    fl_fs_outb(PIC1_CMD, 0x11);
    fl_fs_outb(PIC2_CMD, 0x11);
    fl_fs_outb(PIC1_DATA, 0x20);
    fl_fs_outb(PIC2_DATA, 0x28);
    fl_fs_outb(PIC1_DATA, 4);
    fl_fs_outb(PIC2_DATA, 2);
    fl_fs_outb(PIC1_DATA, 1);
    fl_fs_outb(PIC2_DATA, 1);
    fl_fs_outb(PIC1_DATA, 0xff);
    fl_fs_outb(PIC2_DATA, 0xff);
}

void fl_fs_pic_unmask(int irq)
{
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(1u << (irq & 7));
    fl_fs_outb(port, (uint8_t)(fl_fs_inb(port) & (uint8_t)~bit));
}

void fl_fs_pic_eoi(int irq)
{
    if (irq >= 8)
        fl_fs_outb(PIC2_CMD, 0x20);
    fl_fs_outb(PIC1_CMD, 0x20);
}
