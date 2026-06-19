/**
 * **P5-4 — Dirty page writeback (`flush` / `bdi-writeback` analog)** (module contract).
 *
 * **Distribution:** writeback batches dirty **P5-3** pages to **P4-4** block backends
 * via **P1-8** scheduling. Tag **`FL_BG_JOB_TAG_WRITEBACK`**. Handlers may perform
 * bounded block I/O (may block on I/O, not on unbounded sleep).
 *
 * **Depends:** **P5-3** page cache, **P4-4** block, **P1-8**.
 */
#ifndef FL_CONTRACT_P5_WRITEBACK_H
#define FL_CONTRACT_P5_WRITEBACK_H

#include "contract_extend.h"

#define FL_CONTRACT_P5_4_WRITEBACK_CONTRACT_DEFINED 1

/** **fl_wq_work_t.tag** for dirty-page writeback jobs. */
#define FL_BG_JOB_TAG_WRITEBACK 0x0504u

#endif /* FL_CONTRACT_P5_WRITEBACK_H */
