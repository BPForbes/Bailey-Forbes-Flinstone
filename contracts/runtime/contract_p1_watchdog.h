/**
 * **P1-10 — Watchdog / health monitor** (module contract, normative).
 *
 * **Distribution:** health checks are scheduled on **P1-8** with
 * **`FL_BG_JOB_TAG_WATCHDOG`** at **FL_WQ_LAYER_URGENT** so they are not starved by
 * background reclaim or net timer work.
 *
 * **Depends:** **P1-7** timekeeping, **P0-5** tick observability, **P1-8** workqueue.
 */
#ifndef FL_CONTRACT_P1_WATCHDOG_H
#define FL_CONTRACT_P1_WATCHDOG_H

#include "contract_p1_workqueue.h"

#define FL_CONTRACT_P1_10_WATCHDOG_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for watchdog / health-monitor jobs. */
#define FL_BG_JOB_TAG_WATCHDOG 0x0110u

#endif /* FL_CONTRACT_P1_WATCHDOG_H */
