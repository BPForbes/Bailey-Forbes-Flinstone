#ifndef NET_UDP_H
#define NET_UDP_H

#include "contract_p3_udp.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Build UDP header + **payload** into **buf** (host-order ports).
 * Returns total L4 length (8 + payload_len) or 0 on error.
 */
size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len);

#endif /* NET_UDP_H */
