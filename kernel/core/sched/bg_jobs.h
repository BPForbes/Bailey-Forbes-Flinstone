#ifndef FL_BG_JOBS_H
#define FL_BG_JOBS_H

#include "contract_p1_reclaim.h"
#include "contract_p1_watchdog.h"
#include "contract_result.h"

void fl_bg_jobs_init(void);
void fl_bg_jobs_shutdown(void);

/** Periodic tick from shell idle, timer IRQ, or test harness. */
void fl_bg_jobs_tick(unsigned max_wq_items);

fl_result_t fl_bg_job_reclaim_kick(void);
fl_result_t fl_bg_job_watchdog_kick(void);

#endif /* FL_BG_JOBS_H */
