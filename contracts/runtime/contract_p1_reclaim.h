/**
 * **P1-9 — Memory reclaim job (`kswapd` analog)** (module contract, normative).
 *
 * **Distribution:** reclaim work is scheduled on **P1-8** **`fl_wq_default()`** with
 * **`FL_BG_JOB_TAG_RECLAIM`** and default layer **FL_WQ_LAYER_BACKGROUND**. Handlers
 * must not block indefinitely (**contract_p1_workqueue.h**).
 *
 * **Depends:** **P1-4** PMM, **P1-5** arenas, **P1-8** workqueue.
 */
#ifndef FL_CONTRACT_P1_RECLAIM_H
#define FL_CONTRACT_P1_RECLAIM_H

#include "contract_p1_workqueue.h"

#define FL_CONTRACT_P1_9_RECLAIM_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for memory-reclaim jobs. */
#define FL_BG_JOB_TAG_RECLAIM 0x0109u

#endif /* FL_CONTRACT_P1_RECLAIM_H */
