/**
 * **P3-12 — DHCP client (IPv4)** (module contract, normative).
 *
 * Obligations:
 *   - Minimal **DISCOVER → OFFER → REQUEST → ACK** state machine (**RFC 2131**) with
 *     **RFC 2132** options subset explicitly listed in implementation docs.
 *   - **Finite timeouts** at each state; behaviour on **NAK** and on **lease expiry**.
 *   - Builds on **P3-6** / **P3-5**; document interaction with **static routes** and **P3-4**.
 *   - **PX-12** netboot paths may depend on this row—keep interchange surfaces stable once
 *     marked **✅** in the roadmap snapshot.
 *
 * See **docs/ROADMAP.md** Phase 3 and **PX-12** notes.
 */
#ifndef FL_CONTRACT_P3_DHCP_H
#define FL_CONTRACT_P3_DHCP_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_12_DHCP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_DHCP_H */
