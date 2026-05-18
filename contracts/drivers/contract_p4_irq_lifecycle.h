/**
 * **P4-2 — IRQ lifecycle** (module contract, normative).
 *
 * **Distribution:** **hardirq** handlers deliver **minimal** work to the CPU that raised
 * the interrupt; **bottom halves** (threaded IRQ, workqueue analogue, or softirq-style
 * deferral) own blocking, allocation, and driver **TX/RX** continuation. **No sleep in
 * true hardirq** is a **normative** rule for **B** or **K** builds that claim this row;
 * debug builds may assert it where the arch permits.
 *
 * **Outcomes:** IRQ registration and deferral setup return **`fl_result_t`** (**P0-2**).
 * **MSI** / **MSI-X** routing is coordinated with **P4-3**; **P4-5** event-ring service
 * uses the bottom-half path defined here.
 */
#ifndef FL_CONTRACT_P4_IRQ_LIFECYCLE_H
#define FL_CONTRACT_P4_IRQ_LIFECYCLE_H

#include "contract_extend.h"

#define FL_CONTRACT_P4_2_IRQ_LIFECYCLE_CONTRACT_DEFINED 1

/** Debug / **B** builds: hardirq handlers must not sleep (review + assert hook). */
#define FL_CONTRACT_P4_2_HARDIRQ_NO_SLEEP 1

/** Bottom-half may block; hardirq may only queue work to this path. */
#define FL_CONTRACT_P4_2_BOTTOM_HALF_DEFERRAL_REQUIRED 1

#endif /* FL_CONTRACT_P4_IRQ_LIFECYCLE_H */
