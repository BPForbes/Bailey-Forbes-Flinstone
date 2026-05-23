/* P7 shell batch argv grouping (module contract, normative). Batch mode consumes
 * contiguous argv tokens per builtin (`contracts`, `audit`) so `contracts audit show N`
 * runs **contracts** then **audit**, not a merged token. Implemented by **fl_batch_*_tokens_count**
 * in userland/shell/util.c; caps below are review hooks. See docs/ROADMAP.md **TODO: P7**. */
#ifndef FL_CONTRACT_P7_SHELL_BATCH_H
#define FL_CONTRACT_P7_SHELL_BATCH_H

#include "contract_extend.h"

#define FL_CONTRACT_P7_SHELL_BATCH_CONTRACT_DEFINED 1

/** Max tokens consumed by `audit show [N]` in batch mode (including `audit`). */
#define FL_CONTRACT_P7_BATCH_AUDIT_SHOW_MAX_TOKENS 3u

/** Max tokens consumed by `contracts summary|json|--help|-h` in batch mode. */
#define FL_CONTRACT_P7_BATCH_CONTRACTS_QUALIFIED_MAX_TOKENS 2u

/** Upper bound on argv tokens one batch grouping step may consume (lab builds). */
#define FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL 16u

_Static_assert(FL_CONTRACT_P7_BATCH_AUDIT_SHOW_MAX_TOKENS >= 2u,
               "audit show batch must include audit + show");
_Static_assert(FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL >=
                   FL_CONTRACT_P7_BATCH_AUDIT_SHOW_MAX_TOKENS,
               "total batch argv budget must cover audit show grouping");
_Static_assert(FL_CONTRACT_P7_BATCH_MAX_TOKENS_TOTAL >=
                   FL_CONTRACT_P7_BATCH_CONTRACTS_QUALIFIED_MAX_TOKENS,
               "total batch argv budget must cover contracts grouping");

#endif /* FL_CONTRACT_P7_SHELL_BATCH_H */
