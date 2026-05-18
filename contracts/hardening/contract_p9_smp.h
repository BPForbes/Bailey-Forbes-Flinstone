/* **P9-3 — SMP bring-up (B)** (module contract, normative).
 *
 * **Distribution:** **Secondary CPUs** are brought up via **architecture firmware contracts**
 * (**PSCI `CPU_ON`** on AArch64 per **P4-7** and **ARM DEN0022**; **x86_64 AP** entry per **Intel
 * SDM**). **Per-CPU data**, **IPIs**, and **memory ordering** compose **P1-3** (preemption / lock
 * ordering) and **P1-6** (driver reentrancy)—**P9** records **scale-out vocabulary** (which barriers
 * apply across CPUs, which locks are IRQ-safe) without duplicating the full **P1** scheduler model.
 *
 * **Outcomes:** bring-up failures return **`fl_result_t`** (**P0-2**) at the orchestration boundary.
 */
#ifndef FL_CONTRACT_P9_SMP_H
#define FL_CONTRACT_P9_SMP_H

#include "contract_extend.h"

#define FL_CONTRACT_P9_3_SMP_CONTRACT_DEFINED 1

/** IPI or cross-call handlers must obey the same lock-order graph as hardirq/BH paths (P1-3). */
#define FL_CONTRACT_P9_SMP_LOCK_ORDER_INHERITS_P1 1

/** AArch64 secondaries: PSCI CPU_ON from DT cpus + psci nodes (P4-7) before per-CPU init. */
#define FL_CONTRACT_P9_SMP_AARCH64_PSCI_CPU_ON 1

_Static_assert(FL_CONTRACT_P9_SMP_LOCK_ORDER_INHERITS_P1 == 1,
               "SMP contract must stay aligned with P1 lock ordering");

#endif /* FL_CONTRACT_P9_SMP_H */
