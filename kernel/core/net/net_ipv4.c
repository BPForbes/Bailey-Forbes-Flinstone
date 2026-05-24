#include "net_ipv4.h"

#include "net_checksum.h"

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

void fl_net_ipv4_format_addr(uint32_t addr_be, char *buf, size_t buf_len) {
    if (!buf || buf_len < 8u)
        return;
    snprintf(buf, buf_len, "%u.%u.%u.%u", (unsigned)(addr_be & 0xffu),
             (unsigned)((addr_be >> 8) & 0xffu), (unsigned)((addr_be >> 16) & 0xffu),
             (unsigned)((addr_be >> 24) & 0xffu));
}

size_t fl_net_ipv4_build(fl_net_ipv4_hdr_t *hdr, uint8_t *buf, size_t cap, uint8_t proto,
                         uint32_t src_be, uint32_t dst_be, const void *payload,
                         size_t payload_len, uint16_t id_be) {
    size_t total = FL_NET_IPV4_HDR_LEN_MIN + payload_len;
    uint16_t csum;

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
    buf[12] = (uint8_t)(src_be >> 24);
    buf[13] = (uint8_t)(src_be >> 16);
    buf[14] = (uint8_t)(src_be >> 8);
    buf[15] = (uint8_t)(src_be);
    buf[16] = (uint8_t)(dst_be >> 24);
    buf[17] = (uint8_t)(dst_be >> 16);
    buf[18] = (uint8_t)(dst_be >> 8);
    buf[19] = (uint8_t)(dst_be);

    if (payload && payload_len > 0)
        memcpy(buf + FL_NET_IPV4_HDR_LEN_MIN, payload, payload_len);

    csum = fl_net_ipv4_checksum(buf, FL_NET_IPV4_HDR_LEN_MIN);
    buf[10] = (uint8_t)(csum >> 8);
    buf[11] = (uint8_t)(csum & 0xff);
    hdr->hdr_csum_be = csum;
    return total;
}
