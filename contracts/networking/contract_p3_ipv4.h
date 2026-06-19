/**
 * **P3-5 — IPv4** (module contract, normative).
 *
 * **Distribution:** **IPv4** headers and payloads appear as **fl_net_frame_view_t** slices
 * with **fl_ipv4_be32_t** addresses in **network byte order**. Prefer **fl_net_packet_t**
 * (**contract_p3_packet.h**) for multi-layer parse/build pipelines; **ICMP echo** payloads
 * are bounded **l4** slices over the same vocabulary.
 *
 * **Checksums:** software checksum is the default contract; offload bits live in
 * **fl_net_drv_caps_t** (**P3-1**).
 */
#ifndef FL_CONTRACT_P3_IPV4_H
#define FL_CONTRACT_P3_IPV4_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_5_IPV4_CONTRACT_DEFINED 1

/** **IPv4** protocol field (**RFC 790** names used on the wire). */
#define FL_NET_IP_PROTO_ICMP 1u
#define FL_NET_IP_PROTO_TCP 6u
#define FL_NET_IP_PROTO_UDP 17u

/** **ICMP** message types used by **P3-5** echo path (**RFC 792**). */
#define FL_NET_ICMPV4_TYPE_ECHO_REPLY 0u
#define FL_NET_ICMPV4_TYPE_ECHO 8u

/** Longest IPv4 prefix length for interchange docs (CIDR **slash** notation). */
#define FL_NET_IPV4_MAX_PREFIX_LEN 32u

#endif /* FL_CONTRACT_P3_IPV4_H */
