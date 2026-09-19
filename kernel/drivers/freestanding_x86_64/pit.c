#include "pit.h"
#include "ioport.h"

void fl_fs_pit_init(void)
{
    /* Channel 0, ~100 Hz. */
    fl_fs_outb(0x43, 0x36);
    fl_fs_outb(0x40, 0x9b);
    fl_fs_outb(0x40, 0x2e);
}
