#include "net_dns.h"

#include "net_ipv4.h"
#include "net_wire_host.h"

#include <stdio.h>
#include <string.h>

static int dns_encode_name(const char *host, uint8_t *out, size_t cap) {
    const char *p = host;
    uint8_t *len_ptr;
    size_t pos = 0;

    if (!host || !out || cap < 2)
        return -1;

    while (*p) {
        const char *dot = strchr(p, '.');
        size_t labellen = dot ? (size_t)(dot - p) : strlen(p);
        if (labellen == 0 || labellen > 63 || pos + 1 + labellen >= cap)
            return -1;
        len_ptr = out + pos++;
        *len_ptr = (uint8_t)labellen;
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

static int dns_read_nameserver(uint32_t *out_be) {
    FILE *f;
    char line[256];

    f = fopen("/etc/resolv.conf", "r");
    if (!f)
        return 0;

    while (fgets(line, sizeof(line), f)) {
        unsigned o[4];
        if (sscanf(line, " nameserver %u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) == 4 ||
            sscanf(line, "nameserver %u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) == 4) {
            fclose(f);
            *out_be = (uint32_t)o[0] | ((uint32_t)o[1] << 8) | ((uint32_t)o[2] << 16) |
                      ((uint32_t)o[3] << 24);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static fl_result_t dns_query_a(const char *host, uint32_t *out_addr_be) {
    uint8_t query[256];
    uint8_t answer[512];
    size_t qlen;
    size_t alen;
    int nlen;
    uint32_t ns_be = 0;
    uint16_t txid = 0x4f4c;

    if (!dns_read_nameserver(&ns_be))
        return FL_RESULT_ERR;

    memset(query, 0, sizeof(query));
    query[0] = (uint8_t)(txid >> 8);
    query[1] = (uint8_t)(txid & 0xff);
    query[2] = 0x01;
    query[5] = 0x01;
    nlen = dns_encode_name(host, query + 12, sizeof(query) - 12);
    if (nlen < 0)
        return FL_RESULT_INVAL;
    qlen = (size_t)(12 + nlen);
    query[qlen++] = 0;
    query[qlen++] = 1;
    query[qlen++] = 0;
    query[qlen++] = 1;

    if (fl_net_wire_send_udp(ns_be, 40053, 53, query, qlen, answer, sizeof(answer), &alen,
                             4000u) != FL_RESULT_OK)
        return FL_RESULT_TIMEDOUT;

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
            if ((answer[off] & 0xc0u) == 0xc0u) {
                off += 2;
                break;
            }
            if (answer[off] == 0) {
                off++;
                break;
            }
            off += 1u + (size_t)answer[off];
        }
        if (off + 10 > alen)
            return FL_RESULT_ERR;
        rtype = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        rclass = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        off += 4; /* TTL */
        rdlen = (uint16_t)(((uint16_t)answer[off] << 8) | answer[off + 1]);
        off += 2;
        if (rtype != 1u || rclass != 1u || rdlen != 4u || off + 4 > alen)
            return FL_RESULT_ERR;
        *out_addr_be = (uint32_t)answer[off] | ((uint32_t)answer[off + 1] << 8) |
                       ((uint32_t)answer[off + 2] << 16) | ((uint32_t)answer[off + 3] << 24);
        return FL_RESULT_OK;
    }
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
