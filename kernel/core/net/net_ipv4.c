#include "net_ipv4.h"

#include "net_checksum.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t ipv4_pack_octets(unsigned o0, unsigned o1, unsigned o2, unsigned o3) {
    return (uint32_t)o0 | ((uint32_t)o1 << 8) | ((uint32_t)o2 << 16) | ((uint32_t)o3 << 24);
}

int fl_net_ipv4_is_loopback(uint32_t addr_be) {
    return (addr_be & 0xffu) == (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET;
}

int fl_net_ipv4_parse_literal(const char *s, uint32_t *out_be) {
    unsigned o[4];
    unsigned i;
    const char *p = s;
    char *end;

    if (!s || !out_be)
        return 0;

    for (i = 0; i < 4; i++) {
        unsigned long v;
        if (!p || !*p)
            return 0;
        v = strtoul(p, &end, 10);
        if (end == p || v > 255u)
            return 0;
        o[i] = (unsigned)v;
        p = end;
        if (i < 3) {
            if (*p != '.')
                return 0;
            p++;
        } else if (*p != '\0')
            return 0;
    }

    *out_be = ipv4_pack_octets(o[0], o[1], o[2], o[3]);
    return 1;
}

int fl_net_ipv6_wire_to_v4(const uint8_t addr_be[16], uint32_t *v4_be_out)
{
    static const uint8_t loop6[16] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const uint8_t mapped_prefix[12] =
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

    if (!addr_be || !v4_be_out)
        return 0;
    if (memcmp(addr_be, loop6, sizeof(loop6)) == 0) {
        *v4_be_out = ipv4_pack_octets(127u, 0u, 0u, 1u);
        return 1;
    }
    if (memcmp(addr_be, mapped_prefix, sizeof(mapped_prefix)) == 0) {
        *v4_be_out = ipv4_pack_octets(addr_be[12], addr_be[13], addr_be[14], addr_be[15]);
        return 1;
    }
    return 0;
}

static int parse_endpoint_port(const char *port_str, uint16_t *port_out)
{
    long port;
    char *end = NULL;

    if (!port_str || !port_out)
        return 0;
    errno = 0;
    port = strtol(port_str, &end, 10);
    if (errno != 0 || end == port_str || (end && *end != '\0'))
        return 0;
    if (port <= 0 || port > 65535)
        return 0;
    *port_out = (uint16_t)port;
    return 1;
}

int fl_net_endpoint_parse_v4(const char *s, uint32_t *addr_be_out, uint16_t *port_out)
{
    char buf[128];
    char host[128];
    size_t n;
    const char *port_str;

    if (!s || !addr_be_out || !port_out)
        return 0;
    n = strnlen(s, sizeof(buf));
    if (n == 0u || n >= sizeof(buf))
        return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';

    if (buf[0] == '[') {
        char *end_br = strchr(buf, ']');
        size_t host_len;
        if (!end_br || end_br[1] != ':')
            return 0;
        host_len = (size_t)(end_br - (buf + 1));
        if (host_len == 0u || host_len >= sizeof(host))
            return 0;
        memcpy(host, buf + 1, host_len);
        host[host_len] = '\0';
        port_str = end_br + 2;
        if (fl_net_ipv4_parse_literal(host, addr_be_out))
            return parse_endpoint_port(port_str, port_out);
        {
            unsigned char raw[16];
            if (inet_pton(AF_INET6, host, raw) != 1)
                return 0;
            if (!fl_net_ipv6_wire_to_v4(raw, addr_be_out))
                return 0;
            return parse_endpoint_port(port_str, port_out);
        }
    }

    {
        char *colon = strrchr(buf, ':');
        if (!colon || colon == buf)
            return 0;
        *colon = '\0';
        port_str = colon + 1;
        if (!fl_net_ipv4_parse_literal(buf, addr_be_out))
            return 0;
        return parse_endpoint_port(port_str, port_out);
    }
}

void fl_net_ipv4_format_addr(uint32_t addr_be, char *buf, size_t buf_len) {
    if (!buf || buf_len < 8u)
        return;
    snprintf(buf, buf_len, "%u.%u.%u.%u", (unsigned)(addr_be & 0xffu),
             (unsigned)((addr_be >> 8) & 0xffu), (unsigned)((addr_be >> 16) & 0xffu),
             (unsigned)((addr_be >> 24) & 0xffu));
}

uint32_t fl_net_ipv4_network_addr(uint32_t addr_be, uint8_t prefix_len) {
    uint32_t net_be = addr_be;
    unsigned i;

    if (prefix_len > 32u)
        prefix_len = 32u;

    for (i = 0; i < 4u; i++) {
        unsigned bits;
        uint8_t mask;

        if (prefix_len <= i * 8u) {
            net_be &= ~((uint32_t)0xffu << (i * 8u));
            continue;
        }
        bits = (unsigned)prefix_len - i * 8u;
        if (bits >= 8u)
            continue;
        mask = (uint8_t)(0xffu << (8u - bits));
        net_be &= ((uint32_t)mask << (i * 8u));
    }
    return net_be;
}

size_t fl_net_ipv4_build(fl_net_ipv4_hdr_t *hdr, uint8_t *buf, size_t cap, uint8_t proto,
                         uint32_t src_be, uint32_t dst_be, const void *payload,
                         size_t payload_len, uint16_t id_be) {
    size_t total;
    uint16_t csum;
    const size_t max_payload = (size_t)UINT16_MAX - FL_NET_IPV4_HDR_LEN_MIN;

    if (payload_len > 0 && !payload)
        return 0;
    if (payload_len > max_payload)
        return 0;
    total = FL_NET_IPV4_HDR_LEN_MIN + payload_len;
    if (total > (size_t)UINT16_MAX)
        return 0;

    if (!buf || cap < total || !hdr)
        return 0;

    hdr->ver_ihl = FL_NET_IPV4_VER_IHL;
    hdr->tos = 0;
    hdr->total_len_be = (uint16_t)total;
    hdr->id_be = id_be;
    hdr->frag_be = 0;
    hdr->ttl = 64;
    hdr->protocol = proto;
    hdr->hdr_csum_be = 0;
    hdr->src_be = src_be;
    hdr->dst_be = dst_be;

    buf[0] = hdr->ver_ihl;
    buf[1] = hdr->tos;
    buf[2] = (uint8_t)(total >> 8);
    buf[3] = (uint8_t)(total & 0xff);
    buf[4] = (uint8_t)(hdr->id_be >> 8);
    buf[5] = (uint8_t)(hdr->id_be & 0xff);
    buf[6] = (uint8_t)(hdr->frag_be >> 8);
    buf[7] = (uint8_t)(hdr->frag_be & 0xff);
    buf[8] = hdr->ttl;
    buf[9] = hdr->protocol;
    buf[10] = 0;
    buf[11] = 0;
    buf[12] = (uint8_t)(src_be & 0xffu);
    buf[13] = (uint8_t)((src_be >> 8) & 0xffu);
    buf[14] = (uint8_t)((src_be >> 16) & 0xffu);
    buf[15] = (uint8_t)((src_be >> 24) & 0xffu);
    buf[16] = (uint8_t)(dst_be & 0xffu);
    buf[17] = (uint8_t)((dst_be >> 8) & 0xffu);
    buf[18] = (uint8_t)((dst_be >> 16) & 0xffu);
    buf[19] = (uint8_t)((dst_be >> 24) & 0xffu);

    if (payload && payload_len > 0)
        memcpy(buf + FL_NET_IPV4_HDR_LEN_MIN, payload, payload_len);

    csum = fl_net_ipv4_checksum(buf, FL_NET_IPV4_HDR_LEN_MIN);
    buf[10] = (uint8_t)(csum >> 8);
    buf[11] = (uint8_t)(csum & 0xff);
    hdr->hdr_csum_be = csum;
    return total;
}
