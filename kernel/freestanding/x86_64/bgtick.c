/*
 * Freestanding tick for the shared workqueue (kernel/core/sched, built with -DFL_FREESTANDING).
 * The PIT interrupt only counts and flags; fl_fs_shell_run calls fl_fs_bg_tick between interrupts,
 * so the queue is polled from one place with interrupts' work deferred and needs no lock. The queue's
 * clock is the PIT tick count (10 ms steps): the image has no calendar clock and does not compile
 * timekeeping.c.
 */
#include "bg_jobs.h"
#include "pit.h"
#include "shell.h"
#include "timekeeping.h"

fl_result_t fl_time_monotonic_ns(int64_t *out_ns)
{
    if (!out_ns)
        return FL_RESULT_INVAL;
    *out_ns = (int64_t)fl_fs_pit_ticks() * FL_FS_PIT_TICK_NS;
    return FL_RESULT_OK;
}

void fl_fs_bg_tick(void)
{
    fl_bg_jobs_tick(FL_BG_TICK_MAX_ITEMS);
}
