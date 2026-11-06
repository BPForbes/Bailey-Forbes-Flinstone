#include "pit.h"
#include "ioport.h"

void fl_fs_pit_init(void)
{
    /* Channel 0, ~100 Hz. */
    fl_fs_outb(0x43, 0x36);
    fl_fs_outb(0x40, 0x9b);
    fl_fs_outb(0x40, 0x2e);
}

static volatile uint64_t s_ticks;
static volatile int s_tick_pending;

void fl_fs_pit_irq(void)
{
    s_ticks++;
    s_tick_pending = 1;
}

uint64_t fl_fs_pit_ticks(void)
{
    return s_ticks;
}

int fl_fs_pit_pending_take(void)
{
    int p = s_tick_pending;

    s_tick_pending = 0;
    return p;
}
