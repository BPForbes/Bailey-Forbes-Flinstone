/* **P8-1 — Device timing fidelity** (module contract, normative).
 *
 * **Distribution:** **Replay logs** and **hosted device models** expose a **total order** of
 * externally visible events (NIC DMA completions, IRQ edges, timer ticks) sufficient for
 * `make test_replay` style regression. **Wall-clock** sources may differ between lab and CI;
 * contracts record **which time bases** are comparable, not raw nanosecond equality on silicon.
 *
 * **Outcomes:** scheduling or parsing errors return **`fl_result_t`** in **FL_RESULT_MIN** …
 * **FL_RESULT_MAX** (**P0-2**).
 *
 * **QEMU lab:** optional **deterministic instruction timing** via QEMU **`-icount`** (or
 * equivalent) is a **hosted lab** technique for shrinking flaky timing windows; it does not
 * replace **B**-path wall-clock discipline.
 */
#ifndef FL_CONTRACT_P8_TIMING_H
#define FL_CONTRACT_P8_TIMING_H

#include "contract_extend.h"

#define FL_CONTRACT_P8_1_TIMING_CONTRACT_DEFINED 1

/** Replay event streams must remain parse-forward: consumers never seek unspecified offsets. */
#define FL_CONTRACT_P8_REPLAY_FORWARD_ONLY 1

/** Lab may document a bounded skew between emulated IRQ latency and host scheduling jitter. */
#define FL_CONTRACT_P8_TIMING_JITTER_DOCUMENTED_LAB 1

_Static_assert(FL_CONTRACT_P8_REPLAY_FORWARD_ONLY == 1,
               "replay contracts must stay forward-parseable");

#endif /* FL_CONTRACT_P8_TIMING_H */
