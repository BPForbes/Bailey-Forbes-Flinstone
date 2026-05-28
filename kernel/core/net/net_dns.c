#include "net_dns.h"

#include "net_ipv4.h"
#include "net_packet.h"
#include "net_wire_host.h"

#include "fl/net_asm.h"

#include <stdio.h>
#include <string.h>

#ifndef FL_NET_DNS_NS_MAX
#define FL_NET_DNS_NS_MAX 3u
#endif

#ifndef FL_NET_DNS_QUERY_RETRIES
#define FL_NET_DNS_QUERY_RETRIES 2u
#endif

static int dns_encode_name(const char *host, uint8_t *out, size_t cap) {
    const char *p = host;
    size_t pos = 0;

    if (!host || !out || cap < 2)
        return -1;

    while (*p) {
        const char *dot = strchr(p, '.');
        size_t labellen = dot ? (size_t)(dot - p) : strlen(p);
        if (labellen == 0 || labellen > 63 || pos + 1 + labellen >= cap)
            return -1;
        out[pos++] = (uint8_t)labellen;
        memcpy(out + pos, p, labellen);
        pos += labellen;
        if (!dot)
            break;
        p = dot + 1;
    }
    if (pos + 1 >= cap)
        return -1;
    out[pos++] = 0;
    return (int)pos;
}

static unsigned dns_read_nameservers(uint32_t *ns_be, unsigned ns_cap) {
    FILE *f;
    char line[256];
    unsigned count = 0;

    if (!ns_be || ns_cap == 0u)
        return 0;

    f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f) && count < ns_cap) {
        unsigned o[4];
        if (sscanf(line, " nameserver %u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) == 4 ||
            sscanf(line, "nameserver %u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) == 4) {
            ns_be[count++] = (uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16) |
                             ((uint32_t)o[3] << 24);
        }
    }
    fclose(f);
    return count;
}

static uint16_t dns_next_txid(void) {
    static uint16_t s_txid = 0x4f4cu;
    s_txid = (uint16_t)(s_txid + 0x1317u);
    return s_txid;
}

static fl_result_t dns_query_a_once(const char *host, uint32_t ns_be, uint16_t txid,
                                    uint32_t *out_addr_be) {
    uint8_t query[256];
    uint8_t answer[512];
    size_t qlen;
    size_t alen;
    int nlen;
    fl_result_t udp_rc;

    memset(query, 0, sizeof(query));
#if defined(FL_NET_ASM_AVAILABLE)
    asm_net_dns_query_header_prefix(query, txid);
#else
    query[0] = (uint8_t)(txid >> 8);
    query[1] = (uint8_t)(txid & 0xff);
    query[2] = 0x01;
    query[5] = 0x01;
#endif
    nlen = dns_encode_name(host, query + 12, sizeof(query) - 12);
    if (nlen < 0)
        return FL_RESULT_INVAL;
    qlen = (size_t)(12 + nlen);
    if (qlen + 4 > sizeof(query))
        return FL_RESULT_INVAL;
    query[qlen++] = 0;
    query[qlen++] = 1;
    query[qlen++] = 0;
    query[qlen++] = 1;

    {
        fl_net_packet_t query_pkt;
        fl_net_packet_t answer_pkt;

        udp_rc = fl_net_packet_bind_l4(&query_pkt, query, sizeof(query), 0u, qlen);
        if (udp_rc != FL_RESULT_OK)
            return udp_rc;
        udp_rc = fl_net_wire_send_udp_pkt(ns_be, 40053, 53, &query_pkt, &answer_pkt, answer,
                                          sizeof(answer), &alen, 4000u);
        if (udp_rc != FL_RESULT_OK)
            return udp_rc;
    }

    if (alen < 12 + 12 + 16)
        return FL_RESULT_ERR;
    if (((uint16_t)answer[0] << 8 | answer[1]) != txid)
        return FL_RESULT_ERR;
    if ((answer[3] & 0x0f) != 0)
        return FL_RESULT_ERR;

    {
        size_t off = 12;
        uint16_t rtype;
        uint16_t rclass;
        uint16_t rdlen;

        while (off < alen) {
            if (answer[off] == 0) {
                off++;
                break;
            }
            if (off + 1u + (size_t)answer[off] > alen)
                return FL_RESULT_ERR;
            off += 1u + (size_t)answer[off];
        }
        if (off + 4 > alen)
            return FL_RESULT_ERR;
        off += 4;

        if (off >= alen)
            return FL_RESULT_ERR;
        if ((answer[off] & 0xc0u) == 0xc0u) {
            if (off + 2 > alen)
                return FL_RESULT_ERR;
            off += 2;
        } else {
            while (off < alen && answer[off] != 0) {
                if (off + 1u + (size_t)answer[off] > alen)
                    return FL_RESULT_ERR;
                off += 1u + (size_t)answer[off];
            }
            if (off >= alen)
                return FL_RESULT_ERR;
            off++;
        }
        if (off + 10 > alen)
            return FL_RESULT_ERR;
        rtype = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        rclass = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        off += 4;
        rdlen = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        if (rtype != 1u || rclass != 1u || rdlen != 4u || off + 4 > alen)
            return FL_RESULT_ERR;
        *out_addr_be = (uint32_t)answer[off] | ((uint32_t)answer[off + 1] << 8) |
                       ((uint32_t)answer[off + 2] << 16) | ((uint32_t)answer[off + 3] << 24);
        return FL_RESULT_OK;
    }
}

static fl_result_t dns_query_a(const char *host, uint32_t *out_addr_be) {
    uint32_t ns_list[FL_NET_DNS_NS_MAX];
    unsigned ns_count;
    fl_result_t last = FL_RESULT_ERR;

    ns_count = dns_read_nameservers(ns_list, FL_NET_DNS_NS_MAX);
    if (ns_count == 0u)
        return FL_RESULT_ERR;

    for (unsigned n = 0; n < ns_count; n++) {
        for (unsigned attempt = 0; attempt < FL_NET_DNS_QUERY_RETRIES; attempt++) {
            uint16_t txid = dns_next_txid();
            fl_result_t rc = dns_query_a_once(host, ns_list[n], txid, out_addr_be);

            if (rc == FL_RESULT_OK)
                return FL_RESULT_OK;
            last = rc;
            if (rc == FL_RESULT_INVAL)
                return rc;
        }
    }
    return last;
}

fl_result_t fl_net_resolve_ipv4(const char *host, uint32_t *out_addr_be, char *resolved_ip,
                                size_t resolved_ip_len) {
    if (!host || !host[0] || !out_addr_be || !resolved_ip || resolved_ip_len < 8u)
        return FL_RESULT_INVAL;

    if (strcmp(host, "localhost") == 0) {
        *out_addr_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
        fl_net_ipv4_format_addr(*out_addr_be, resolved_ip, resolved_ip_len);
        return FL_RESULT_OK;
    }

    if (fl_net_ipv4_parse_literal(host, out_addr_be)) {
        fl_net_ipv4_format_addr(*out_addr_be, resolved_ip, resolved_ip_len);
        return FL_RESULT_OK;
    }

    {
        fl_result_t dns_rc = dns_query_a(host, out_addr_be);
        if (dns_rc != FL_RESULT_OK)
            return dns_rc;
    }

    fl_net_ipv4_format_addr(*out_addr_be, resolved_ip, resolved_ip_len);
    return FL_RESULT_OK;
}
