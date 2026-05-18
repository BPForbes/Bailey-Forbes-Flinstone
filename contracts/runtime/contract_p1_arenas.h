/**
 * **P1-5 — Memory domain arenas** (module contract, normative).
 *
 * Obligations:
 *   - Memory domains that must be **libc-free** on **DRIVERS_BAREMETAL** use **fixed
 *     arenas** with documented sizes and visible exhaustion behaviour—not discarded
 *     `malloc` wrappers posing as domains.
 *   - **Appendix D** §2.1 and execution row **6** apply to arena wiring reviews.
 *   - Exhaustion returns **`FL_RESULT_NOMEM`** or **`FL_RESULT_BUSY`** (**P0-2**).
 *
 * Implementation spans **mem_domain** and related alloc paths; this file is the
 * **contract anchor** for domain sizing policy.
 */
#ifndef FL_CONTRACT_P1_ARENAS_H
#define FL_CONTRACT_P1_ARENAS_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_5_ARENAS_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_ARENAS_H */
