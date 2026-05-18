/**
 * **P1-6 — Driver model reentrancy** (module contract, normative).
 *
 * Obligations:
 *   - **`s_model_lock`** (and related locks) guard static driver registration tables,
 *     IRQ dispatch tables, and teardown paths that may race with probes or ISRs.
 *   - **Appendix D** §2.4 and execution row **7** define the review and stress
 *     expectations until dedicated HW CI exists.
 *
 *   - Probe/remove races surface **`fl_result_t`** (**P0-2**); **P4-1** driver v2
 *     lifecycle inherits the same outcome channel.
 *
 * Implementation: **kernel/drivers/driver_model.c** and friends; this header is the
 * **contract anchor** for concurrent probe and remove paths.
 */
#ifndef FL_CONTRACT_P1_DRIVER_REENTRANCY_H
#define FL_CONTRACT_P1_DRIVER_REENTRANCY_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_6_DRIVER_REENTRANCY_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_DRIVER_REENTRANCY_H */
