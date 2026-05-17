/**
 * **P4-2 — IRQ lifecycle** (module contract, normative).
 *
 * **Distribution:** **hardirq** handlers deliver **minimal** work to the CPU that raised
 * the interrupt; **bottom halves** (threaded IRQ, workqueue analogue, or softirq-style
 * deferral) own blocking, allocation, and driver **TX/RX** continuation. **No sleep in
 * true hardirq** is a **normative** rule for **B** or **K** builds that claim this row;
 * debug builds may assert it where the arch permits.
 */
#ifndef FL_CONTRACT_P4_IRQ_LIFECYCLE_H
#define FL_CONTRACT_P4_IRQ_LIFECYCLE_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_2_IRQ_LIFECYCLE_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P4_IRQ_LIFECYCLE_H */
