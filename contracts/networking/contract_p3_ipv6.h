/**
 * **P3-11 — IPv6 + ICMPv6** (module contract, normative).
 *
 * Dual-stack vocabulary: **RFC 8200**, **RFC 4291**, **RFC 4443**, **RFC 4861**.
 * **FL_ETHERTYPE_IPV6** dispatch is implemented in `kernel/core/net/` alongside
 * the existing IPv4 path.
 */
#ifndef FL_CONTRACT_P3_IPV6_H
#define FL_CONTRACT_P3_IPV6_H

#include "contract_p3_wire.h"

#include <stdint.h>

#define FL_CONTRACT_P3_11_IPV6_CONTRACT_DEFINED 1

#define FL_NET_IPV6_ADDR_LEN 16u
#define FL_NET_IPV6_HDR_LEN 40u
#define FL_NET_IPV6_MAX_PREFIX_LEN 128u

#define FL_NET_IP_PROTO_ICMPV6 58u

#define FL_NET_ICMPV6_HDR_MIN 8u
#define FL_NET_ICMPV6_TYPE_ECHO 128u
#define FL_NET_ICMPV6_TYPE_ECHO_REPLY 129u
#define FL_NET_ICMPV6_TYPE_NS 135u
#define FL_NET_ICMPV6_TYPE_NA 136u

#define FL_NET_NDP_OPT_TARGET_LL 2u
#define FL_NET_NDP_OPT_TARGET 2u /* target link-layer address option type in NA */

#ifndef FL_NET_NDP_CACHE_MAX
#define FL_NET_NDP_CACHE_MAX 16u
#endif

typedef struct {
    uint8_t bytes[FL_NET_IPV6_ADDR_LEN];
} fl_net_ipv6_addr_t;

typedef enum {
    FL_NET_AF_INET4 = 4,
    FL_NET_AF_INET6 = 6
} fl_net_addr_family_t;

/** IPv6 fixed header — 40 bytes (RFC 8200). All multi-byte fields are wire NBO. */
typedef struct __attribute__((packed)) {
    uint32_t ver_tc_flow_be;
    uint16_t payload_len_be;
    uint8_t next_hdr;
    uint8_t hop_limit;
    uint8_t src[FL_NET_IPV6_ADDR_LEN];
    uint8_t dst[FL_NET_IPV6_ADDR_LEN];
} fl_net_ipv6_hdr_t;

typedef struct {
    uint8_t ip6[FL_NET_IPV6_ADDR_LEN];
    uint8_t mac[FL_NET_ETH_ADDR_LEN];
    uint8_t state;
} fl_net_ndp_entry_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum_be;
} fl_net_icmpv6_hdr_t;

#endif /* FL_CONTRACT_P3_IPV6_H */
