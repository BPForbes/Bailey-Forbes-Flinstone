/**
 * **P2-1** — Hosted principal **literals** (no other contract includes).
 *
 * Use this header in translation units that must not pull **contract_runtime.h**
 * (for example public headers under kernel/include/fl/) while still sharing the
 * same **FL_PRINCIPAL** environment variable name and **guest** spelling as the
 * shell implementation.
 *
 * Normative strings: keep **ASCII**, no trailing spaces; changing them is an
 * interchange break — bump **FL_CONTRACT_P2_IDENTITY_REV** in **contract_identity.h**.
 */
#ifndef FL_CONTRACT_P2_PRINCIPAL_NAMES_H
#define FL_CONTRACT_P2_PRINCIPAL_NAMES_H

/** Process environment variable that selects the hosted principal role (Phase 2). */
#define FL_PRINCIPAL_ENV_NAME "FL_PRINCIPAL"

/** Lab / CI **guest** role: restricted builtins and no foreign **execvp**. */
#define FL_PRINCIPAL_GUEST_LITERAL "guest"

#endif /* FL_CONTRACT_P2_PRINCIPAL_NAMES_H */
