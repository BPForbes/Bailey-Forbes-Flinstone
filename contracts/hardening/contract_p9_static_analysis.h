/* **P9-2 — Static analysis / Coverity-class gates** (module contract, normative).
 *
 * **Distribution:** **Static analysis** tools emit **findings** with **tool id**, **file**, **line**,
 * and **severity**. **Release gates** compare **incoming** findings against a **baseline**; new
 * **critical** / **high** (tool-defined) issues block merge unless waived with maintainer text.
 * This header does **not** wrap vendor APIs—it names **caps and enums** other docs reference.
 *
 * **Outcomes:** misconfigured scans return **`fl_result_t`** (**P0-2**), not empty success.
 */
#ifndef FL_CONTRACT_P9_STATIC_ANALYSIS_H
#define FL_CONTRACT_P9_STATIC_ANALYSIS_H

#include "contract_extend.h"

#include <stdint.h>

#define FL_CONTRACT_P9_2_STATIC_ANALYSIS_CONTRACT_DEFINED 1

typedef enum {
    FL_CONTRACT_P9_SA_SEVERITY_INFO = 0,
    FL_CONTRACT_P9_SA_SEVERITY_LOW = 1,
    FL_CONTRACT_P9_SA_SEVERITY_MEDIUM = 2,
    FL_CONTRACT_P9_SA_SEVERITY_HIGH = 3,
    FL_CONTRACT_P9_SA_SEVERITY_CRITICAL = 4
} fl_contract_p9_sa_severity_t;

/** Release gate: no net-new critical findings vs locked baseline (per tool policy). */
#define FL_CONTRACT_P9_SA_GATE_ZERO_NEW_CRITICAL 1

/** Human ruleset pointer for reviews (informative string token, not a runtime path). */
#define FL_CONTRACT_P9_SA_RULESET_SEI_CERT_C "SEI_CERT_C"

_Static_assert((int)FL_CONTRACT_P9_SA_SEVERITY_CRITICAL == 4, "severity enum ordering stable");

#endif /* FL_CONTRACT_P9_STATIC_ANALYSIS_H */
