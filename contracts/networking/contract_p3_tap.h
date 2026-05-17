/**
 * **P3-3 — TAP backend (hosted only)** (module contract, normative).
 *
 * **Distribution:** Ethernet frames cross a host **TUN/TAP** fd as **fl_net_frame_view_t**
 * on TX and **fl_net_frame_mut_t** (or equivalent blocking read) on RX.
 *
 * **Privilege:** opening the device requires **FL_AUTHZ_OP_NETDEV_REGISTER**; sustained
 * raw I/O uses **FL_AUTHZ_OP_NETDEV_IO** (**contract_p3_trust.h**). Shell elevation
 * (**P2-4**) is a separate include when wiring UX.
 *
 * **CI:** **P0-3** — document **SKIP_TAP=1** when runners lack **CAP_NET_ADMIN**.
 */
#ifndef FL_CONTRACT_P3_TAP_H
#define FL_CONTRACT_P3_TAP_H

#include "contract_p3_trust.h"
#include "contract_p3_wire.h"

_Static_assert((int)FL_AUTHZ_OP_NETDEV_REGISTER == 2,
               "P3-3 requires FL_AUTHZ_OP_NETDEV_REGISTER from contract_p2_authz.h");
_Static_assert((int)FL_AUTHZ_OP_NETDEV_IO == 3,
               "P3-3 requires FL_AUTHZ_OP_NETDEV_IO from contract_p2_authz.h");

#define FL_CONTRACT_P3_3_TAP_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_TAP_H */
