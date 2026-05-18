/* P5-3 Page cache (module contract, normative). Cache keys as backing id plus block index;
 * coherency between net and block is explicit per integration. Hot path must not allocate
 * (P1 execution context). APIs return fl_result_t; pressure may use FL_RESULT_BUSY (P0-2).
 * See docs/ROADMAP.md Phase 5. */
#ifndef FL_CONTRACT_P5_PAGE_CACHE_H
#define FL_CONTRACT_P5_PAGE_CACHE_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P5_3_PAGE_CACHE_CONTRACT_DEFINED 1

typedef uint64_t fl_page_cache_backing_id_t;

#define FL_PAGE_CACHE_BACKING_ID_INVALID ((fl_page_cache_backing_id_t)0u)

/** Minimum supported cached block shift (512-byte blocks). */
#define FL_PAGE_CACHE_BLOCK_SHIFT_MIN 9u

/** Maximum supported cached block shift (4096-byte blocks) for lab alignment reviews. */
#define FL_PAGE_CACHE_BLOCK_SHIFT_MAX 12u

typedef enum {
    FL_PAGE_CACHE_PRIORITY_BACKGROUND = 0,
    FL_PAGE_CACHE_PRIORITY_NORMAL = 1,
    FL_PAGE_CACHE_PRIORITY_HOT = 2
} fl_page_cache_priority_t;

/** Soft cap on resident cache lines (lab builds). */
#define FL_PAGE_CACHE_MAX_ENTRIES 4096u

/** Dirty ratio threshold (percent) before writeback pressure is required. */
#define FL_PAGE_CACHE_DIRTY_RATIO_PERCENT_MAX 80u

/** Max blocks flushed per writeback batch (bounded work per tick). */
#define FL_PAGE_CACHE_WRITEBACK_BATCH_MAX 32u

_Static_assert(FL_PAGE_CACHE_BLOCK_SHIFT_MAX >= FL_PAGE_CACHE_BLOCK_SHIFT_MIN,
               "page cache block shift bounds");
_Static_assert(FL_PAGE_CACHE_DIRTY_RATIO_PERCENT_MAX <= 100u,
               "dirty ratio must be a valid percentage");
_Static_assert(FL_PAGE_CACHE_WRITEBACK_BATCH_MAX >= 1u,
               "writeback batch must flush at least one block");

#endif /* FL_CONTRACT_P5_PAGE_CACHE_H */
