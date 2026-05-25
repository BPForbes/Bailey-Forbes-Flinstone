#ifndef FL_BG_JOBS_H
#define FL_BG_JOBS_H

#include "contract_result.h"

/** Domain tags for **fl_wq_work_t.tag** (see **docs/BACKGROUND_JOBS.md**). */
#define FL_BG_JOB_TAG_RECLAIM   0x0109u /* P1-9 */
#define FL_BG_JOB_TAG_WATCHDOG  0x0110u /* P1-10 */
#define FL_BG_JOB_TAG_NET       0x0314u /* P3-14 */
#define FL_BG_JOB_TAG_KWORKER   0x0408u /* P4-8 */
#define FL_BG_JOB_TAG_WRITEBACK 0x0504u /* P5-4 */
#define FL_BG_JOB_TAG_RCU       0x0904u /* P9-4 */

void fl_bg_jobs_init(void);
void fl_bg_jobs_shutdown(void);

/** Periodic tick from shell idle, timer IRQ, or test harness. */
void fl_bg_jobs_tick(unsigned max_wq_items);

fl_result_t fl_bg_job_reclaim_kick(void);
fl_result_t fl_bg_job_watchdog_kick(void);

#endif /* FL_BG_JOBS_H */
