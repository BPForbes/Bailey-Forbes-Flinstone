#ifndef NET_WIRE_EGRESS_H
#define NET_WIRE_EGRESS_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Send an **IPv4** payload (header+body already built, or L4 only with builder) on the
 * routed **netdev** path with **ARP** when needed. Used by ICMP echo on loopback/TAP.
 */
fl_result_t fl_net_wire_egress_l4(uint32_t dst_be, uint8_t ip_proto, const uint8_t *l4,
                                  size_t l4_len, uint8_t *rx_l4, size_t rx_l4_cap,
                                  size_t *rx_l4_len, unsigned timeout_ms, double *out_rtt_ms);

#endif /* NET_WIRE_EGRESS_H */
