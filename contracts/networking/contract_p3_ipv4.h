/**
 * **P3-5 — IPv4** (module contract, normative).
 *
 * Obligations:
 *   - Addresses, **netmask**, **routing table** (at least **default route**), **ICMP echo**
 *     (**ping**) per **RFC 791** + **RFC 792** subset documented here and in tests.
 *   - **Checksum offload** optional; **path MTU** may be stubbed but behaviour must be
 *     explicit (black-hole vs ICMP **Fragmentation Needed** when implemented).
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_IPV4_H
#define FL_CONTRACT_P3_IPV4_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_5_IPV4_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_IPV4_H */
