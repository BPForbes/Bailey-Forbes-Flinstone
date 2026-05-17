/*
 * P2-4 Sudo-like elevation hosted (module contract, normative).
 *
 * Inherits P0 plus P1 through contract_runtime.h.
 *
 * Obligations:
 *   - Describe time-bounded elevation or explicit runas-style entry points on H.
 *   - Require logging of who, when, and why for elevation; TTY-bound confirmation
 *     where ROADMAP calls for it.
 *
 * See docs/ROADMAP.md Phase 2; anchor for sudo.c and related builtins.
 */
#ifndef FL_CONTRACT_P2_ELEVATION_H
#define FL_CONTRACT_P2_ELEVATION_H

#include "contract_runtime.h"

#define FL_CONTRACT_P2_4_ELEVATION_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P2_ELEVATION_H */
