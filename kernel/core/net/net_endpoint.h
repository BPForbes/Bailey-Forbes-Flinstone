#ifndef NET_ENDPOINT_H
#define NET_ENDPOINT_H

#include "contract_p3_wire.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/** Parse `host:port` or `[host]:port`. Supports IPv4, `::1`, v4-mapped IPv6, native IPv6. */
int fl_net_endpoint_parse(const char *s, fl_net_endpoint_t *out);

/** Legacy helper: succeed only when the endpoint maps to IPv4 (`::1`, v4-mapped, or plain v4). */
int fl_net_endpoint_parse_v4(const char *s, uint32_t *addr_be_out, uint16_t *port_out);

int fl_net_ipv6_wire_to_v4(const uint8_t addr_be[16], uint32_t *v4_be_out);
int fl_net_v4_to_mapped_v6(uint32_t v4_be, uint8_t v6_be[16]);

int fl_net_endpoint_to_v4(const fl_net_endpoint_t *ep, uint32_t *v4_be_out);
void fl_net_endpoint_from_v4(uint32_t v4_be, uint16_t port_host, fl_net_endpoint_t *out);
void fl_net_endpoint_from_v6(const uint8_t v6_be[16], uint16_t port_host, fl_net_endpoint_t *out);

int fl_net_endpoint_is_loopback(const fl_net_endpoint_t *ep);

#endif /* NET_ENDPOINT_H */
