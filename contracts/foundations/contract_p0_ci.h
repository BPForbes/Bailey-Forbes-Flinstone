/**
 * **P0-3 — CI realism** (module contract, normative).
 *
 * **Interchange blueprint** (**FL_CONTRACT_P0_CI_INTERCHANGE_REV**): frozen
 * data-distribution model for the default CI gate. Bump the revision only with a
 * **version/entries** note when surface identifiers, ownership, lifetime, or error
 * channel semantics change (no silent edits).
 *
 * Named surface(s):
 *   - **ci_default_gate** — source tree + preset matrix → pass/fail + job artifacts
 *
 * Ownership / lifetime:
 *   - The **CI job** owns workspace artifacts for the job duration (**transient**).
 *   - **Published** artifacts (logs, binaries attached to the run) are **immutable**
 *     after the publish step completes.
 *
 * Error / status channel:
 *   - Non-zero job **exit status** and failing **check annotations** are the
 *     authoritative failure outputs; silent ignore of failing checks is out of contract.
 *
 * Obligations (summary; full matrix lives in **.github/workflows** and **AGENTS.md**):
 *   - Default **CI** runs **unit tests** without special hardware; flaky tests are
 *     quarantined with an issue link rather than silent ignore.
 *   - Optional jobs may enable **compiler hardening** (stack protector, sanitizers,
 *     CET, PAC/BTI) where supported; default release presets are not changed
 *     without **version/entries** note (see roadmap global standard **#8**).
 *   - When **FDT** sources or **DTB** blobs are committed, optional **dtc** checks
 *     align with **P0-7** (**contract_p0_fdt.h**).
 *
 * This header **defines** the contract for planning and reviews; **GitHub Actions**
 * YAML (**c-cpp.yml** `version-requirements` / build jobs) and **make** targets are
 * the **enforcement** surface.
 */
#ifndef FL_CONTRACT_P0_CI_H
#define FL_CONTRACT_P0_CI_H

/** Frozen P0-3 CI interchange blueprint revision (bump with version/entries note only). */
#define FL_CONTRACT_P0_CI_INTERCHANGE_REV 1

/** Named surface: default CI gate (tree + presets → pass/fail + artifacts). */
#define FL_CONTRACT_P0_CI_SURFACE_DEFAULT_GATE 0

/** One past the last **fl_contract_p0_ci_surface_t** value. */
#define FL_CONTRACT_P0_CI_SURFACE_COUNT 1

typedef enum {
    fl_contract_p0_ci_surface_default_gate = FL_CONTRACT_P0_CI_SURFACE_DEFAULT_GATE,
} fl_contract_p0_ci_surface_t;

/** Workspace artifacts: owned by the job until the job ends. */
#define FL_CONTRACT_P0_CI_LIFETIME_TRANSIENT 0
/** Published run outputs: immutable after publish. */
#define FL_CONTRACT_P0_CI_LIFETIME_IMMUTABLE_PUBLISHED 1

/** Authoritative CI failure: non-zero process exit. */
#define FL_CONTRACT_P0_CI_STATUS_EXIT_FAILURE 1
/** Authoritative CI failure: failing check annotation on the run. */
#define FL_CONTRACT_P0_CI_STATUS_CHECK_ANNOTATION 2

#define FL_CONTRACT_P0_3_CI_CONTRACT_DEFINED 1

_Static_assert(FL_CONTRACT_P0_CI_SURFACE_COUNT == 1,
               "fl_contract_p0_ci_surface_t: expected one named surface");

#endif /* FL_CONTRACT_P0_CI_H */
