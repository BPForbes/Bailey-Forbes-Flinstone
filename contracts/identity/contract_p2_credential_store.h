/**
 * **P2-2 — Credential store (hosted)** (module contract, normative).
 *
 * Inherits **P0** plus **P1** through **contract_runtime.h**.
 *
 * **Lab store (4.1.0+):** SQLite database at **FL_CREDENTIAL_STORE_LAB_SQLITE_REL**
 * (same path as **FL_USERS_DB_DEFAULT_PATH** in **user_db.h**). Legacy
 * **passwd.lab** / **shadow.lab** paths are **deprecated** — do not create new
 * loaders for them.
 *
 * Obligations:
 *   - Do **not** store cleartext passwords on disk.
 *   - **H (current):** PBKDF2-HMAC-SHA256 verifiers in SQLite (**password_hash**);
 *     legacy single-pass SHA-256 rows may still verify.
 *   - **Future upgrade:** libsodium **Argon2id** (or equivalent) documented in
 *     **version/entries** before changing verifier format.
 *   - Document threat model (**lab only** vs stronger) beside any real store.
 *
 * See **docs/ROADMAP.md** Phase 2; anchor for **user_db** and auth glue.
 */
#ifndef FL_CONTRACT_P2_CREDENTIAL_STORE_H
#define FL_CONTRACT_P2_CREDENTIAL_STORE_H

#include "contract_runtime.h"

#include <stdint.h>

#define FL_CONTRACT_P2_2_CREDENTIAL_STORE_CONTRACT_DEFINED 1

/** Normative lab credential store (SQLite; gitignored at runtime). */
#define FL_CREDENTIAL_STORE_LAB_SQLITE_REL "userland/shell/fl_users.db"

/** @deprecated Superseded by **FL_CREDENTIAL_STORE_LAB_SQLITE_REL** (GH #128). */
#define FL_CREDENTIAL_STORE_LAB_PASSWD_REL "userland/shell/passwd.lab"

/** @deprecated Superseded by **FL_CREDENTIAL_STORE_LAB_SQLITE_REL** (GH #128). */
#define FL_CREDENTIAL_STORE_LAB_SHADOW_REL "userland/shell/shadow.lab"

/** Inclusive bound for NUL-terminated usernames in lab files and APIs. */
#define FL_CREDENTIAL_USERNAME_MAX_CHARS 63u

/** Minimum plausible encoded verifier length (policy hook; adjust with chosen KDF). */
#define FL_CREDENTIAL_VERIFIER_MIN_CHARS 20u

_Static_assert(FL_CREDENTIAL_USERNAME_MAX_CHARS < 256u, "username cap fits uint8_t if packed");

#endif /* FL_CONTRACT_P2_CREDENTIAL_STORE_H */
