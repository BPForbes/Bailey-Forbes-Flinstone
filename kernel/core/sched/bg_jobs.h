#ifndef FL_BG_JOBS_H
#define FL_BG_JOBS_H

#include "contract_p1_reclaim.h"
#include "contract_p1_watchdog.h"
#include "contract_result.h"
#include "workqueue.h"

void fl_bg_jobs_init(void);
void fl_bg_jobs_shutdown(void);

/** Periodic tick from shell idle, timer IRQ, or test harness. */
void fl_bg_jobs_tick(unsigned max_wq_items);

/** Items one tick may run; the hosted ticker uses it. */
#define FL_BG_TICK_MAX_ITEMS 8u
/** Hosted tick period (the freestanding PIT is ~100 Hz, 10 ms per tick). */
#define FL_BG_TICK_PERIOD_MS 100u

#ifdef FL_WQ_HAS_LOCK
/**
 * Start the hosted ticker thread: it calls **fl_bg_jobs_tick** every **period_ms** and is the
 * only thread that polls the default queue. **FL_RESULT_BUSY** if already running.
 */
fl_result_t fl_bg_jobs_ticker_start(unsigned period_ms);
/** Stop and join the ticker (no-op if not running). Call before **fl_bg_jobs_shutdown**. */
void fl_bg_jobs_ticker_stop(void);
#endif

fl_result_t fl_bg_job_reclaim_kick(void);
fl_result_t fl_bg_job_watchdog_kick(void);

#endif /* FL_BG_JOBS_H */
