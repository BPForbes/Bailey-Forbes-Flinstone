/**
 * **P0-3 — CI realism** (module contract, normative).
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
 * YAML and **make** targets are the **enforcement** surface.
 */
#ifndef FL_CONTRACT_P0_CI_H
#define FL_CONTRACT_P0_CI_H

#define FL_CONTRACT_P0_3_CI_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P0_CI_H */
