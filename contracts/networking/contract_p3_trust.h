/**
 * **P3 ↔ P2 composition (narrow)** — not a second identity layer.
 *
 * **P3** contracts model **octet paths, queues, clocks, and protocol headers** on the
 * wire. **P2** still owns **who may act** (principals, credentials, elevation) and the
 * central **fl_authz_check_fn** story.
 *
 * **fl_net_netdev_set_authz_hook** uses the same contract as **fl_authz_check_fn**; avoid
 * casting unrelated function pointers (**#233**).
 *
 * This header pulls **only** **P2-3** (**contract_p2_authz.h**) so **P3** implementations
 * can reference **FL_AUTHZ_OP_NETDEV_REGISTER** and **FL_AUTHZ_OP_NETDEV_IO** without
 * including the full **contract_identity.h** bundle. Code that needs **FL_PRINCIPAL_***
 * literals or credential stores must include **contract_identity.h** explicitly—that is
 * intentional separation so **P3** is not a textual or include-graph clone of **P2**.
 */
#ifndef FL_CONTRACT_P3_TRUST_H
#define FL_CONTRACT_P3_TRUST_H

#include "contract_p2_authz.h"

#define FL_CONTRACT_P3_COMPOSES_P2_AUTHZ_ONLY 1

#endif /* FL_CONTRACT_P3_TRUST_H */
