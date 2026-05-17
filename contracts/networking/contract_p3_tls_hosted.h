/**
 * **P3-9 — TLS (hosted)** (module contract, normative).
 *
 * Obligations:
 *   - Prefer **userland** TLS (e.g. **mbedTLS** / **OpenSSL**) behind shell commands or
 *     libc integration on **H**; **no** in-kernel TLS on **K/B** until **P1** memory and
 *     **P1-7** time APIs are stable for those tracks.
 *   - **Certificate validation** and **hostname** checks must be explicit (fail closed).
 *   - **Wall-clock** for validity windows ties to **P1-7** / optional NTP on **H** per roadmap.
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_TLS_HOSTED_H
#define FL_CONTRACT_P3_TLS_HOSTED_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_9_TLS_HOSTED_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_TLS_HOSTED_H */
