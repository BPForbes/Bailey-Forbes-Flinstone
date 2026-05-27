/**
 * **P3-1 — Device abstraction (`netdev`)** (module contract, normative).
 *
 * **Distribution (octet path):**
 *   - **TX** accepts **fl_net_frame_view_t** pointing at a full **IEEE 802.3** Ethernet
 *     frame (destination MAC … FCS policy: driver documents whether FCS is in-band).
 *   - **RX** fills **fl_net_frame_mut_t** up to **cap**; **len** is bytes received on success.
 *   - **MTU** on **fl_net_driver_t** is the **L2 payload** contract default
 *     (**FL_NET_ETH_MTU_DEFAULT**) unless the implementation negotiates jumbo and records it.
 *
 * **Privilege (not redefined here):** promiscuous mode and raw **TAP**-class I/O must pass
 * **FL_AUTHZ_OP_NETDEV_REGISTER** / **FL_AUTHZ_OP_NETDEV_IO** via **contract_p3_trust.h**
 * before touching hardware or host fds. **P2** identity headers are **not** included here.
 *
 * **Surfaces:** maps to **FL_CONTRACT_SURFACE_NETDEV** in **contract_foundations.h**;
 * errors on the hot path use **fl_result_t** (**P0-2**).
 *
 * **Lifecycle:** **fl_net_netdev_init** / **fl_net_netdev_shutdown** pair TAP, loopback, authz (**#232**).
 *
 * See **docs/ROADMAP.md** Phase 3; **kernel/include/fl/driver/net.h** mirrors this contract.
 */
#ifndef FL_CONTRACT_P3_NETDEV_H
#define FL_CONTRACT_P3_NETDEV_H

#include "contract_p3_trust.h"
#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_1_NETDEV_CONTRACT_DEFINED 1

_Static_assert((int)FL_CONTRACT_SURFACE_NETDEV == 1, "P3-1 maps to NETDEV surface ordinal");

_Static_assert((int)FL_AUTHZ_OP_NETDEV_REGISTER == 2,
               "P3-1 requires FL_AUTHZ_OP_NETDEV_REGISTER from contract_p2_authz.h");
_Static_assert((int)FL_AUTHZ_OP_NETDEV_IO == 3,
               "P3-1 requires FL_AUTHZ_OP_NETDEV_IO from contract_p2_authz.h");

#endif /* FL_CONTRACT_P3_NETDEV_H */
