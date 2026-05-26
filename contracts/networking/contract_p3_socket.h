/**
 * **P3 socket endpoint model** (normative interchange; full **P3-13** shim TODO).
 *
 * **Distribution:** logical channel **Socket(A,B) = (IP_A, Port_A, IP_B, Port_B)** in
 * network byte order. Application data flows:
 *
 *   App → Socket → UDP/TCP segment → IPv4 → ARP(MAC_B) → Ethernet → NIC
 *
 * **Implementation:** **kernel/core/net/net_background.c** (task backend relay);
 * **net_wire_egress.c** (ARP + routed netdev); **net_udp.c** (UDP L4 build).
 */
#ifndef FL_CONTRACT_P3_SOCKET_H
#define FL_CONTRACT_P3_SOCKET_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_SOCKET_CONTRACT_DEFINED 1

/** Four-tuple for one socket channel (all multi-byte fields **network** order). */
typedef struct {
    fl_ipv4_be32_t local_ip_be;
    fl_port_be16_t local_port_be;
    fl_ipv4_be32_t peer_ip_be;
    fl_port_be16_t peer_port_be;
} fl_net_socket_endpoint_t;

#endif /* FL_CONTRACT_P3_SOCKET_H */
