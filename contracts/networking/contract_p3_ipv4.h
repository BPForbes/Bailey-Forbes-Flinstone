/**
 * **P3-5 — IPv4** (module contract, normative).
 *
 * **Distribution:** **IPv4** headers and payloads appear as **fl_net_frame_view_t** slices
 * with **fl_ipv4_be32_t** addresses in **network byte order**. **ICMP echo** payloads
 * are bounded slices over the same type vocabulary.
 *
 * **Checksums:** software checksum is the default contract; offload bits live in
 * **fl_net_drv_caps_t** (**P3-1**).
 */
#ifndef FL_CONTRACT_P3_IPV4_H
#define FL_CONTRACT_P3_IPV4_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_5_IPV4_CONTRACT_DEFINED 1

#endif /* FL_CONTRACT_P3_IPV4_H */
