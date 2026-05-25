/**
 * **P3 wire vocabulary** — octet-path interchange types and shared constants.
 *
 * This header is **not** identity policy (**P2**): it names **Ethernet / IPv4 / UDP**
 * sizes and **non-owning buffer views** that cross **P3** boundaries. Authorization
 * and principal proof stay in **P2**; see **contract_p3_trust.h** for the narrow
 * **P2-3** hook surface used by privileged netdev operations.
 *
 * **Implementation:** **kernel/core/net/net_wire.c** (**net_wire.h**) enforces
 * **fl_net_frame_view_t** / **fl_net_frame_mut_t** bounds and DIX Ethernet helpers.
 */
#ifndef FL_CONTRACT_P3_WIRE_H
#define FL_CONTRACT_P3_WIRE_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/** Bump when wire caps or public typedef layout changes (P3 umbrella may _Static_assert). */
#define FL_CONTRACT_P3_WIRE_REV 3

/** IEEE 802.3 address length (octets). */
#define FL_NET_ETH_ADDR_LEN 6u

/** Default L2 MTU (payload) for interchange docs; jumbo paths must name a higher cap. */
#define FL_NET_ETH_MTU_DEFAULT 1500u

#define FL_NET_ETH_FCS_LEN 4u

/** DIX Ethernet header length (dst + src + ethertype); **FCS** is driver policy (**P3-1**). */
#define FL_NET_ETH_FRAME_HDR_LEN (FL_NET_ETH_ADDR_LEN + FL_NET_ETH_ADDR_LEN + 2u)

/** Common DIX ethertypes (network byte order constants as **uint16_t** values). */
#define FL_ETHERTYPE_IPV4 0x0800u
#define FL_ETHERTYPE_ARP 0x0806u
#define FL_ETHERTYPE_VLAN 0x8100u
#define FL_ETHERTYPE_IPV6 0x86DDu

#define FL_NET_IPV4_HDR_LEN_MIN 20u
#define FL_NET_IPV4_ADDR_LEN 4u

#define FL_NET_UDP_HDR_LEN 8u
#define FL_NET_TCP_HDR_LEN_MIN 20u

#define FL_NET_ICMPV4_HDR_MIN 8u

/**
 * Default **UDP datagram** ceiling for bounded queues (**P3-6**); single-datagram
 * contract cap distinct from IP fragmentation rules.
 */
#ifndef FL_NET_CONTRACT_MAX_UDP_DATAGRAM
#define FL_NET_CONTRACT_MAX_UDP_DATAGRAM 65507u
#endif

_Static_assert(FL_NET_CONTRACT_MAX_UDP_DATAGRAM >= 1u &&
                   FL_NET_CONTRACT_MAX_UDP_DATAGRAM <= 65507u,
               "FL_NET_CONTRACT_MAX_UDP_DATAGRAM must be 1..65507");

/** Typical MSS for IPv4 over Ethernet (1500 − 20 − 20) unless PMTU says otherwise. */
#define FL_NET_TCP_MSS_IPV4_ETH_DEFAULT 1460u

/** BOOTP/DHCP options magic cookie (**RFC 1497** / **RFC 2131**). */
#define FL_NET_DHCP_MAGIC_COOKIE_BE32 UINT32_C(0x63825363)

typedef uint8_t fl_eth_mac_t[FL_NET_ETH_ADDR_LEN];

/** IPv4 address in **network** (big-endian wire) order inside a **uint32_t**. */
typedef uint32_t fl_ipv4_be32_t;

/** Transport port in **network** byte order. */
typedef uint16_t fl_port_be16_t;

/** Non-owning view of octets (L2 frame, IP packet, or UDP payload) crossing a **P3** API. */
typedef struct {
    const uint8_t *data;
    size_t len;
} fl_net_frame_view_t;

/** Writable RX target: **data** and **cap** are caller-owned storage. */
typedef struct {
    uint8_t *data;
    size_t len; /**< Filled length on successful **recv** (<= **cap**). */
    size_t cap;
} fl_net_frame_mut_t;

/** Feature bits advertised or enabled on a netdev (**P3-1**). */
typedef uint32_t fl_net_drv_caps_t;
#define FL_NET_DRV_CAP_PROMISC ((fl_net_drv_caps_t)1u << 0)
#define FL_NET_DRV_CAP_RX_CSUM_OFFLOAD ((fl_net_drv_caps_t)1u << 1)
#define FL_NET_DRV_CAP_TX_CSUM_OFFLOAD ((fl_net_drv_caps_t)1u << 2)

_Static_assert(FL_NET_ETH_ADDR_LEN == 6u, "Ethernet address is 6 octets");

#endif /* FL_CONTRACT_P3_WIRE_H */
