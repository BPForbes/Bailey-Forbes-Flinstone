/**
 * **P4-8 — Deferred IRQ work (`kworker` analog)** (module contract, normative).
 *
 * **Distribution:** **P4-2** hardirq paths enqueue short descriptors onto **P1-8**
 * workqueues; bottom-half handlers may take locks allowed by **P1-3**. Tag
 * **`FL_BG_JOB_TAG_KWORKER`** identifies IRQ-deferred jobs.
 *
 * **Depends:** **P4-2** IRQ lifecycle, **P1-3**, **P1-8**.
 */
#ifndef FL_CONTRACT_P4_KWORKER_H
#define FL_CONTRACT_P4_KWORKER_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_8_KWORKER_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for deferred IRQ / bottom-half work. */
#define FL_BG_JOB_TAG_KWORKER 0x0408u

#endif /* FL_CONTRACT_P4_KWORKER_H */
