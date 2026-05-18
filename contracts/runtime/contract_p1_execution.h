/**
 * **P1-1 — Execution context** (module contract, normative).
 *
 * Obligations:
 *   - Define what a **task** is for each track: **H** (POSIX thread as unit of work),
 *     **K/B** (kernel thread or equivalent as the unit advances).
 *   - Document **signal-safety** rules for any callback that may run from driver or
 *     interrupt-simulation paths on **H** (async-signal-safe subset where applicable).
 *   - Fallible task or context transitions report **`fl_result_t`** (**P0-2**); use
 *     **FL_RESULT_MIN** / **FL_RESULT_MAX** when persisting outcomes on the wire.
 *
 * See **docs/ROADMAP.md** Phase 1 table; implementation spreads across scheduler and
 * shell glue; this header is the **contract anchor** for reviews.
 */
#ifndef FL_CONTRACT_P1_EXECUTION_H
#define FL_CONTRACT_P1_EXECUTION_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_1_EXECUTION_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_EXECUTION_H */
