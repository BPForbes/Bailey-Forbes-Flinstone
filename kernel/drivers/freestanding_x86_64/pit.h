#ifndef FL_FREESTANDING_PIT_H
#define FL_FREESTANDING_PIT_H

#include <stdint.h>

/** PIT channel 0 runs at ~100 Hz: one tick is 10 ms. */
#define FL_FS_PIT_TICK_NS 10000000ll

void fl_fs_pit_init(void);
/** Called from the timer interrupt: counts the tick and flags it. Nothing else runs there. */
void fl_fs_pit_irq(void);
/** Ticks since fl_fs_pit_init. */
uint64_t fl_fs_pit_ticks(void);
/** Nonzero once per tick burst: returns and clears the flag the interrupt set. */
int fl_fs_pit_pending_take(void);

#endif
