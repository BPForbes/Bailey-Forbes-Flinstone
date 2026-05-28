#ifndef NET_WIRE_EGRESS_H
#define NET_WIRE_EGRESS_H

#include "contract_p3_packet.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Send an L4 payload (ip_proto, l4, l4_len); builds the IPv4 header internally
 * (does not accept a prebuilt IPv4 packet). Uses the routed netdev path with ARP
 * when needed. Used by ICMP echo on loopback/TAP.
 */
fl_result_t fl_net_wire_egress_l4(uint32_t dst_be, uint8_t ip_proto, const uint8_t *l4,
                                  size_t l4_len, uint8_t *rx_l4, size_t rx_l4_cap,
                                  size_t *rx_l4_len, unsigned timeout_ms, double *out_rtt_ms);

/**
 * One-way L4 transmit: route → **ARP(IP_dst)** → build IPv4/Ethernet → netdev TX.
 * Does not wait for a reply (UDP/datagram friendly). Falls back to **FL_RESULT_NOENT**
 * when no routed netdev path exists (**FL_RESULT_NOENT**; no Linux datagram shim).
 */
fl_result_t fl_net_wire_egress_l4_xmit(uint32_t dst_be, uint8_t ip_proto, const uint8_t *l4,
                                       size_t l4_len, unsigned arp_timeout_ms);

/** **l4_pkt** must have **FL_NET_PKT_VALID_L4** (ICMP, UDP datagram, etc.). */
fl_result_t fl_net_wire_egress_l4_pkt(uint32_t dst_be, uint8_t ip_proto,
                                      const fl_net_packet_t *l4_pkt, uint8_t *rx_l4,
                                      size_t rx_l4_cap, size_t *rx_l4_len, unsigned timeout_ms,
                                      double *out_rtt_ms);

fl_result_t fl_net_wire_egress_l4_xmit_pkt(uint32_t dst_be, uint8_t ip_proto,
                                           const fl_net_packet_t *l4_pkt,
                                           unsigned arp_timeout_ms);

#endif /* NET_WIRE_EGRESS_H */
