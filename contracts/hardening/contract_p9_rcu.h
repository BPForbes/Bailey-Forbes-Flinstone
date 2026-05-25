/**
 * **P9-4 — RCU grace-period jobs (`rcuop` / `rcuc` analog)** (module contract).
 *
 * **Distribution:** deferred free runs after **P9-3** SMP read-side critical sections
 * end. Scheduled on **P1-8** with tag **`FL_BG_JOB_TAG_RCU`**. Handlers must respect
 * **P1-3** memory ordering.
 *
 * **Depends:** **P9-3** SMP, **P1-3**, **P1-8**.
 */
#ifndef FL_CONTRACT_P9_RCU_H
#define FL_CONTRACT_P9_RCU_H

#include "contract_extend.h"

#define FL_CONTRACT_P9_4_RCU_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for RCU grace-period work. */
#define FL_BG_JOB_TAG_RCU 0x0904u

#endif /* FL_CONTRACT_P9_RCU_H */
