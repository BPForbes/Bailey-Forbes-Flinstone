/**
 * **P3-3 — TAP backend (hosted only)** (module contract, normative).
 *
 * Obligations:
 *   - Read/write **IEEE 802.3 Ethernet** frames via host **TUN/TAP**; **H** track only.
 *   - **Privilege:** root or **`CAP_NET_ADMIN`** (or documented lab flag); never silent
 *     failure when capability is missing—return **`fl_result_t`**-style errors through
 *     the caller’s channel (**P0-2**).
 *   - **CI:** use **virt** runners or **`SKIP_TAP=1`** with an explicit reason in logs
 *     (**P0-3** realism).
 *
 * See **docs/ROADMAP.md** Phase 3.
 */
#ifndef FL_CONTRACT_P3_TAP_H
#define FL_CONTRACT_P3_TAP_H

#include "contract_identity.h"

#define FL_CONTRACT_P3_3_TAP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_TAP_H */
