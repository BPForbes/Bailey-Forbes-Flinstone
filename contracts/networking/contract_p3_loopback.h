/**
 * **P3-2 — Loopback (software)** (module contract, normative).
 *
 * Obligations:
 *   - Minimal **IPv4** `127.0.0.0/8` path: **ICMP echo** to self and **UDP echo** or
 *     equivalent demux so tests run **without** `/dev/net/tun` (**P3-3**).
 *   - **RFC 1122** host-requirements **subset**; document what is intentionally omitted.
 *   - Bounded buffers; drop policy under pressure must match **P3-6** queue story where
 *     UDP is involved.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_LOOPBACK_H
#define FL_CONTRACT_P3_LOOPBACK_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_2_LOOPBACK_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_LOOPBACK_H */
