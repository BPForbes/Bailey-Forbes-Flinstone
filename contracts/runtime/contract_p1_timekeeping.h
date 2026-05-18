/**
 * **P1-7 — Timekeeping** (module contract, normative).
 *
 * Obligations:
 *   - **H:** **`clock_gettime(CLOCK_MONOTONIC)`** (POSIX.1) is the reference clock for
 *     hosted timing unless a narrower contract is documented per subsystem.
 *   - **AArch64 K/B:** name which **ARM Generic Timer** counter and control registers
 *     back monotonic time (**ARM ARM** §D11).
 *   - **x86_64 K/B:** align **P0-5** PIT or APIC or HPET story in one architecture doc
 *     so **P3-7** (TCP RTO), **P3-9** (TLS time checks), and **P6** timestamps share a
 *     single declared source.
 *   - Optional **RFC 5905** (NTP) on **H** is lab-only and not an **A2** gate.
 *   - Timer setup failures on **B** or **K** paths return **`fl_result_t`** (**P0-2**) where
 *     the API is fallible (distinct from **P0-5** tick observability checks).
 *
 * See **docs/ROADMAP.md** Phase 1; tie-ins to **P1-1** scheduling assumptions.
 */
#ifndef FL_CONTRACT_P1_TIMEKEEPING_H
#define FL_CONTRACT_P1_TIMEKEEPING_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_7_TIMEKEEPING_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P1_TIMEKEEPING_H */
