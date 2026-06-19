#ifndef NET_ETH_H
#define NET_ETH_H

/**
 * Ethernet helpers — thin aliases over **net_wire.h** (**contract_p3_wire.h**).
 */
#include "net_wire.h"

#define FL_NET_LOOPBACK_MAC_HOST_0 0x02u
#define FL_NET_LOOPBACK_MAC_HOST_1 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_2 0x5eu
#define FL_NET_LOOPBACK_MAC_HOST_3 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_4 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_5 0x01u
#define FL_NET_LOOPBACK_MAC_PEER_5 0x02u

#define fl_net_eth_build_ipv4 fl_net_wire_build_eth_ipv4
#define fl_net_eth_parse_ipv4 fl_net_wire_parse_eth_ipv4

#endif /* NET_ETH_H */
