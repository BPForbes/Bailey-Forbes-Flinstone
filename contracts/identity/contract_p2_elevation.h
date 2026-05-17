/**
 * **P2-4 — Sudo-like elevation (hosted)** (module contract, normative).
 *
 * Inherits **P0** plus **P1** through **contract_runtime.h**.
 *
 * **Elevation token** is an opaque **uint64** cookie issued after successful
 * confirmation; **FL_ELEVATION_TOKEN_NONE** means “no elevation”. Concrete issuance,
 * TTL enforcement, and TTY binding live in userland (**sudo.c** / builtins) — this
 * header fixes the **interchange shape** only.
 *
 * Obligations:
 *   - Log **who**, **when**, and **why** on grant and revoke (**P6**).
 *   - Prefer TTY-bound confirmation where the ROADMAP calls for it.
 *   - Lab builds may cap TTL at **FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS** unless policy
 *     documents a wider window.
 *
 * See **docs/ROADMAP.md** Phase 2; anchor for **sudo.c** and related builtins.
 */
#ifndef FL_CONTRACT_P2_ELEVATION_H
#define FL_CONTRACT_P2_ELEVATION_H

#include "contract_runtime.h"

#include <stdint.h>

#define FL_CONTRACT_P2_4_ELEVATION_CONTRACT_DEFINED 1

typedef uint64_t fl_elevation_token_t;

#define FL_ELEVATION_TOKEN_NONE ((fl_elevation_token_t)0u)

/** NUL-terminated human reason stored or logged beside elevation (hosted). */
#define FL_ELEVATION_REASON_MAX_CHARS 255u

/** Soft cap for time-bounded elevation on lab **H** builds (seconds). */
#define FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS (15u * 60u)

_Static_assert(FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS > 0u, "elevation TTL positive");

#endif /* FL_CONTRACT_P2_ELEVATION_H */
