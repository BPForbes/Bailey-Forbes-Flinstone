/*
 * P2-2 Credential store hosted (module contract, normative).
 *
 * Inherits P0 plus P1 through contract_runtime.h.
 *
 * Obligations:
 *   - Document where credentials live on H (config path, in-repo lab layout) and
 *     forbid plaintext-at-rest where ROADMAP requires host crypto.
 *   - State threat model (lab only vs stronger) and rotation or absence policy.
 *
 * See docs/ROADMAP.md Phase 2; anchor for userland config and auth glue.
 */
#ifndef FL_CONTRACT_P2_CREDENTIAL_STORE_H
#define FL_CONTRACT_P2_CREDENTIAL_STORE_H

#include "contract_runtime.h"

#define FL_CONTRACT_P2_2_CREDENTIAL_STORE_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P2_CREDENTIAL_STORE_H */
