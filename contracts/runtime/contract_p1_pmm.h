/**
 * **P1-4 — Physical frame allocator (PMM)** (module contract, normative).
 *
 * Obligations:
 *   - On **B** builds, physical frames are tracked through **`pmm_alloc_frame`** /
 *     **`pmm_free_frame`** (see **kernel/core/mm/pmm.h**); do not silently substitute
 *     libc **`aligned_alloc`** as a “page” where the PMM contract applies.
 *   - **Appendix D** §3.3 and execution row **11** govern stress and visibility of
 *     free-frame accounting.
 *   - **`pmm_alloc_frame`** / **`pmm_free_frame`** report **`fl_result_t`** (**P0-2**).
 *
 * This header names the API contract; implementation lives in **pmm.c**.
 */
#ifndef FL_CONTRACT_P1_PMM_H
#define FL_CONTRACT_P1_PMM_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_4_PMM_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_PMM_H */
