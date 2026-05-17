/**
 * **P3-2 — Loopback (software)** (module contract, normative).
 *
 * **Distribution:** software path delivers **IPv4** datagrams whose destination is in
 * **127.0.0.0/8** without requiring **P3-3** TAP. Frames may still be modeled as
 * **fl_net_frame_view_t** / **fl_ipv4_be32_t** views over host memory.
 *
 * **Constants:** loopback prefix octet for documentation and tests.
 */
#ifndef FL_CONTRACT_P3_LOOPBACK_H
#define FL_CONTRACT_P3_LOOPBACK_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_2_LOOPBACK_CONTRACT_DEFINED 1

/** First octet of IPv4 loopback space (**RFC 1122** §3.2.1.3 g). */
#define FL_NET_IPV4_LOOPBACK_FIRST_OCTET 127u

#endif /* FL_CONTRACT_P3_LOOPBACK_H */
