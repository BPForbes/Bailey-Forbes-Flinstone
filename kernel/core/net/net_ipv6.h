#ifndef NET_IPV6_H
#define NET_IPV6_H

#include "contract_p3_ipv6.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

void fl_net_ipv6_loopback_addr(uint8_t addr[FL_NET_IPV6_ADDR_LEN]);

int fl_net_ipv6_is_loopback(const uint8_t addr[FL_NET_IPV6_ADDR_LEN]);
int fl_net_ipv6_is_link_local(const uint8_t addr[FL_NET_IPV6_ADDR_LEN]);
int fl_net_ipv6_prefix_match(const uint8_t addr[FL_NET_IPV6_ADDR_LEN],
                             const uint8_t net[FL_NET_IPV6_ADDR_LEN], uint8_t prefix_len);

int fl_net_ipv6_parse(const uint8_t *ip6, size_t len, size_t *payload_off, size_t *payload_len,
                      uint8_t *next_hdr_out);

size_t fl_net_ipv6_build(const uint8_t *src, const uint8_t *dst, uint8_t next_hdr,
                         const void *payload, size_t payload_len, uint8_t *out, size_t cap);

uint16_t fl_net_ipv6_icmp6_checksum(const uint8_t *src, const uint8_t *dst,
                                    const uint8_t *icmp6, size_t icmp6_len);

#endif /* NET_IPV6_H */
