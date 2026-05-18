/**
 * **P2-1 — Principal model** (module contract, normative).
 *
 * Inherits **P0** plus **P1** through **contract_runtime.h** (execution context ties
 * to **who** runs). **FL_PRINCIPAL** literals live in **contract_p2_principal_names.h**
 * so **fl/shell_authz.h** and the shell can agree without duplicating strings.
 *
 * Obligations:
 *   - **H**: principal is selected from the environment (**FL_PRINCIPAL_ENV_NAME**);
 *     **FL_PRINCIPAL_GUEST_LITERAL** enables deny-by-default subsets (see **P2-3**).
 *   - **K** / **B**: use **fl_principal_id_t** / **fl_principal_kind_t** for future
 *     service-layer binding; today **FL_PRINCIPAL_ID_LAB** is the interoperable default.
 *   - Align least-privilege defaults with **docs/ROADMAP.md** Phase 2.
 *
 * See **docs/ROADMAP.md** Phase 2; anchor for shell and service glue reviews.
 */
#ifndef FL_CONTRACT_P2_PRINCIPAL_H
#define FL_CONTRACT_P2_PRINCIPAL_H

#include "contract_p2_principal_names.h"
#include "contract_runtime.h"

#include <stdint.h>

#define FL_CONTRACT_P2_1_PRINCIPAL_CONTRACT_DEFINED 1

/** Opaque numeric principal handle for interchange (hosted lab and future K/B). */
typedef uint32_t fl_principal_id_t;

/** Well-known IDs — extend only with a **.ver** note and **FL_CONTRACT_P2_IDENTITY_REV** bump. */
#define FL_PRINCIPAL_ID_UNKNOWN ((fl_principal_id_t)0u)
#define FL_PRINCIPAL_ID_LAB ((fl_principal_id_t)1u)
#define FL_PRINCIPAL_ID_GUEST ((fl_principal_id_t)2u)

typedef enum {
    FL_PRINCIPAL_KIND_UNKNOWN = 0,
    /** Default / elevated lab operator (policy: allow unless overridden). */
    FL_PRINCIPAL_KIND_LAB = 1,
    /** Restricted hosted role (**FL_PRINCIPAL_GUEST_LITERAL**). */
    FL_PRINCIPAL_KIND_GUEST = 2,
    /** Time-bounded elevation (**P2-4**); token carried out-of-band until wired. */
    FL_PRINCIPAL_KIND_ELEVATED = 3
} fl_principal_kind_t;

_Static_assert(sizeof(fl_principal_id_t) == 4u, "fl_principal_id_t width");

#endif /* FL_CONTRACT_P2_PRINCIPAL_H */
