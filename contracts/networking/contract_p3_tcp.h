/**
 * **P3-7 — TCP (large)** (module contract, normative).
 *
 * Obligations:
 *   - Reliable **byte stream**; start with a **single** client/server pair if needed for
 *     bring-up, then generalize with explicit contract updates (**append-only** policy,
 *     **P0-1**).
 *   - **RFC 793** baseline; selective **RFC 5681** congestion behaviour documented when added.
 *   - **RTO** and RTT use **P1-7** clock; **interop** tests against host tools (**nc**,
 *     **socat**) are part of acceptance, not an optional extra.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_TCP_H
#define FL_CONTRACT_P3_TCP_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_7_TCP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_TCP_H */
