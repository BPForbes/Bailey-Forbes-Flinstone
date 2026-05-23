/**
 * **P2-2 — Credential store (hosted)** (module contract, normative).
 *
 * Inherits **P0** plus **P1** through **contract_runtime.h**.
 *
 * **Relative paths** are from the repository root on **H** (lab layout). On-disk
 * files are optional until the store is implemented; these macros are the
 * **normative** locations for reviews and future loaders.
 *
 * Obligations:
 *   - Do **not** store cleartext passwords; on **H** use **crypt(3)** / **libsodium**
 *     (see ROADMAP); at-rest fields must be verifier strings / KDF outputs only.
 *   - Document threat model (**lab only** vs stronger) beside any real store.
 *
 * See **docs/ROADMAP.md** Phase 2; anchor for userland config and auth glue.
 */
#ifndef FL_CONTRACT_P2_CREDENTIAL_STORE_H
#define FL_CONTRACT_P2_CREDENTIAL_STORE_H

#include "contract_runtime.h"

#include <stdint.h>

#define FL_CONTRACT_P2_2_CREDENTIAL_STORE_CONTRACT_DEFINED 1

/** Optional lab passwd table (username → metadata); create only when implementing P2-2. */
#define FL_CREDENTIAL_STORE_LAB_PASSWD_REL "userland/shell/passwd.lab"

/** Optional verifier table (username → hash); must not hold cleartext passwords. */
#define FL_CREDENTIAL_STORE_LAB_SHADOW_REL "userland/shell/shadow.lab"

/** Inclusive bound for NUL-terminated usernames in lab files and APIs. */
#define FL_CREDENTIAL_USERNAME_MAX_CHARS 63u

/** Minimum plausible encoded verifier length (policy hook; adjust with chosen KDF). */
#define FL_CREDENTIAL_VERIFIER_MIN_CHARS 20u

_Static_assert(FL_CREDENTIAL_USERNAME_MAX_CHARS < 256u, "username cap fits uint8_t if packed");

#endif /* FL_CONTRACT_P2_CREDENTIAL_STORE_H */
