#ifndef NET_IPV4_H
#define NET_IPV4_H

#include "contract_p3_ipv4.h"
#include "contract_p3_loopback.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_IPV4_VER_IHL 0x45u

typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len_be;
    uint16_t id_be;
    uint16_t frag_be;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t hdr_csum_be;
    uint32_t src_be;
    uint32_t dst_be;
} fl_net_ipv4_hdr_t;

int fl_net_ipv4_is_loopback(uint32_t addr_be);
int fl_net_ipv4_parse_literal(const char *s, uint32_t *out_be);
/** Parse `host:port` or `[host]:port` (#280). IPv6 literals map ::1 and ::ffff:v4 only. */
int fl_net_endpoint_parse_v4(const char *s, uint32_t *addr_be_out, uint16_t *port_out);
/** Map IPv6 wire bytes from OP_CTRL_HOST_PROMOTE6 to IPv4 when ::1 or v4-mapped. */
int fl_net_ipv6_wire_to_v4(const uint8_t addr_be[16], uint32_t *v4_be_out);
void fl_net_ipv4_format_addr(uint32_t addr_be, char *buf, size_t buf_len);
uint32_t fl_net_ipv4_network_addr(uint32_t addr_be, uint8_t prefix_len);

size_t fl_net_ipv4_build(fl_net_ipv4_hdr_t *hdr, uint8_t *buf, size_t cap, uint8_t proto,
                         uint32_t src_be, uint32_t dst_be, const void *payload,
                         size_t payload_len, uint16_t id_be);

/* #267 / #240: PMTU at fl_net_wire_check_tx; no ICMP PTB. No checksum offload on kernel/bare builds. */

#endif /* NET_IPV4_H */
