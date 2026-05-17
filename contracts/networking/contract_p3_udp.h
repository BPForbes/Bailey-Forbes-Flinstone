/**
 * **P3-6 — UDP** (module contract, normative).
 *
 * Obligations:
 *   - Demux by **port**; **checksum** generation/validation rules per **RFC 768** subset.
 *   - **Socket buffer caps** and **drop policy** under pressure are part of the contract
 *     (document in phase notes / tests, not only in code).
 *   - Uses **P1-7** reference clock for timeouts where applicable.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_UDP_H
#define FL_CONTRACT_P3_UDP_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_6_UDP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_UDP_H */
