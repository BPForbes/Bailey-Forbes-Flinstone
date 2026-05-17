/**
 * **P3-8 — DNS client** (module contract, normative).
 *
 * Obligations:
 *   - Resolver for **A** records; **AAAA** optional but **timeouts** and **retry caps**
 *     must be specified for every record type offered.
 *   - **RFC 1035** semantics **subset**; document maximum response size and truncation handling.
 *   - Transport may use **P3-6** / **P3-7** as implemented; name the stack binding in reviews.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_DNS_H
#define FL_CONTRACT_P3_DNS_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_8_DNS_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_DNS_H */
