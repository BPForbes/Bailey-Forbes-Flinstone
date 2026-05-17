/*
 * P2-3 Authorization middleware (module contract, normative).
 *
 * Inherits P0 plus P1 through contract_runtime.h (use fl_result_t and FL_AUTHZ_* from
 * contract_result.h and contract_auth.h via the foundations aggregate).
 *
 * Obligations:
 *   - Central can_* style checks before FileManager, netdev, mount, and similar
 *     privileged surfaces; document deny-by-default posture.
 *   - Tie deny and allow paths to audit logging expectations (Phase 6 cross-links).
 *
 * See docs/ROADMAP.md Phase 2 and P2 to P3 gate notes.
 */
#ifndef FL_CONTRACT_P2_AUTHZ_H
#define FL_CONTRACT_P2_AUTHZ_H

#include "contract_runtime.h"

#define FL_CONTRACT_P2_3_AUTHZ_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P2_AUTHZ_H */
