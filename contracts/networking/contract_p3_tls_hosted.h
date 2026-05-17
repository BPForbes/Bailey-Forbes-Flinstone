/**
 * **P3-9 — TLS (hosted)** (module contract, normative).
 *
 * **Distribution:** cleartext application octets cross the **P3** stack; **TLS** record
 * framing stays in **userland** libraries on **H** (see roadmap). No TLS ciphertext type is
 * frozen here—only the **boundary**: kernel-shaped builds do not claim TLS until **P1**
 * memory and **P1-7** time contracts are satisfied for those tracks.
 *
 * **Identity:** trust anchors, peer identity, and certificate hostname checks pull in
 * **P2** credential and policy headers in the **same translation unit** that configures TLS;
 * this shard intentionally does **not** `#include "contract_identity.h"` so **P3** stays
 * distinct from **P2**.
 */
#ifndef FL_CONTRACT_P3_TLS_HOSTED_H
#define FL_CONTRACT_P3_TLS_HOSTED_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_9_TLS_HOSTED_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_TLS_HOSTED_H */
