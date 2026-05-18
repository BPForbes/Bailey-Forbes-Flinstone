/**
 * **P1-3 — Preemption contract** (module contract, normative).
 *
 * Obligations:
 *   - Maintain a **lock ordering graph** in `docs/` naming subsystem lock ranks.
 *   - No unbounded work while holding **spinlocks**; document **irq-safe** rules even
 *     when hosted IRQ is simulated.
 *   - **Lock-free** fast paths follow global standard **#6** in the roadmap
 *     (`stdatomic.h` vs explicit arch fences)—do not invent ad-hoc atomics.
 *   - Ordering must remain compatible with future **P9-3** SMP (IPI boundaries), even
 *     if current builds are single-core.
 *   - Lock acquisition failures that surface to callers use **`fl_result_t`** (**P0-2**).
 *
 * See **docs/ROADMAP.md** Phase 1; **Appendix D** execution rows for spinlock races.
 */
#ifndef FL_CONTRACT_P1_PREEMPTION_H
#define FL_CONTRACT_P1_PREEMPTION_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_3_PREEMPTION_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_PREEMPTION_H */
