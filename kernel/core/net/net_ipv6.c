#include "net_ipv6.h"

#include "net_checksum.h"
#include "net_endian.h"

#include <string.h>

void fl_net_ipv6_loopback_addr(uint8_t addr[FL_NET_IPV6_ADDR_LEN]) {
    if (!addr)
        return;
    memset(addr, 0, FL_NET_IPV6_ADDR_LEN);
    addr[15] = 1u;
}

int fl_net_ipv6_is_loopback(const uint8_t addr[FL_NET_IPV6_ADDR_LEN]) {
    uint8_t lb[FL_NET_IPV6_ADDR_LEN];
    if (!addr)
        return 0;
    fl_net_ipv6_loopback_addr(lb);
    return memcmp(addr, lb, FL_NET_IPV6_ADDR_LEN) == 0;
}

int fl_net_ipv6_is_link_local(const uint8_t addr[FL_NET_IPV6_ADDR_LEN]) {
    if (!addr)
        return 0;
    return addr[0] == 0xfeu && (addr[1] & 0xc0u) == 0x80u;
}

int fl_net_ipv6_prefix_match(const uint8_t addr[FL_NET_IPV6_ADDR_LEN],
                             const uint8_t net[FL_NET_IPV6_ADDR_LEN], uint8_t prefix_len) {
    unsigned full_bytes;
    uint8_t mask;

    if (!addr || !net)
        return 0;
    if (prefix_len > FL_NET_IPV6_MAX_PREFIX_LEN)
        return 0;
    if (prefix_len == 0)
        return 1;

    full_bytes = (unsigned)prefix_len / 8u;
    if (full_bytes > 0 && memcmp(addr, net, full_bytes) != 0)
        return 0;

    if (prefix_len % 8u == 0)
        return 1;
    mask = (uint8_t)(0xffu << (8u - (prefix_len % 8u)));
    return (addr[full_bytes] & mask) == (net[full_bytes] & mask);
}

int fl_net_ipv6_parse(const uint8_t *ip6, size_t len, size_t *payload_off, size_t *payload_len,
                      uint8_t *next_hdr_out) {
    uint16_t plen;

    if (!ip6 || len < FL_NET_IPV6_HDR_LEN || !payload_off || !payload_len)
        return 0;
    if ((ip6[0] >> 4) != 6u)
        return 0;

    plen = (uint16_t)(((uint16_t)ip6[4] << 8) | ip6[5]);
    if ((size_t)plen + FL_NET_IPV6_HDR_LEN > len)
        return 0;

    *payload_off = FL_NET_IPV6_HDR_LEN;
    *payload_len = (size_t)plen;
    if (next_hdr_out)
        *next_hdr_out = ip6[6];
    return 1;
}

size_t fl_net_ipv6_build(const uint8_t *src, const uint8_t *dst, uint8_t next_hdr,
                         const void *payload, size_t payload_len, uint8_t *out, size_t cap) {
    size_t total;

    if (!src || !dst || !out)
        return 0;
    if (payload_len > 0 && !payload)
        return 0;
    total = FL_NET_IPV6_HDR_LEN + payload_len;
    if (cap < total)
        return 0;

    memset(out, 0, FL_NET_IPV6_HDR_LEN);
    out[0] = (uint8_t)(6u << 4);
    fl_net_put_u16_be(&out[4], (uint16_t)payload_len);
    out[6] = next_hdr;
    out[7] = 64u;
    memcpy(out + 8, src, FL_NET_IPV6_ADDR_LEN);
    memcpy(out + 24, dst, FL_NET_IPV6_ADDR_LEN);
    if (payload_len > 0)
        memcpy(out + FL_NET_IPV6_HDR_LEN, payload, payload_len);
    return total;
}

uint16_t fl_net_ipv6_icmp6_checksum(const uint8_t *src, const uint8_t *dst,
                                    const uint8_t *icmp6, size_t icmp6_len) {
    return fl_net_ipv6_pseudo_checksum_icmp6(src, dst, icmp6, icmp6_len);
}
