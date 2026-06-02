#include "net_icmpv6.h"

#include "net_endian.h"
#include "net_ipv6.h"

#include <string.h>

size_t fl_net_icmpv6_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                        size_t payload_len) {
    size_t need;
    uint16_t csum;
    uint8_t lb[FL_NET_IPV6_ADDR_LEN];

    if (!buf || cap < FL_NET_ICMPV6_HDR_MIN)
        return 0;
    need = FL_NET_ICMPV6_HDR_MIN + payload_len;
    if (need > cap)
        return 0;

    buf[0] = (uint8_t)FL_NET_ICMPV6_TYPE_ECHO;
    buf[1] = 0;
    fl_net_put_u16_be(buf + 2, 0u);
    fl_net_put_u16_be(buf + 4, id);
    fl_net_put_u16_be(buf + 6, seq);
    if (payload_len > 0)
        memset(buf + FL_NET_ICMPV6_HDR_MIN, 0x5a, payload_len);

    fl_net_ipv6_loopback_addr(lb);
    csum = fl_net_ipv6_icmp6_checksum(lb, lb, buf, need);
    fl_net_put_u16_be(buf + 2, csum);
    return need;
}

int fl_net_icmpv6_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq) {
    if (!buf || len < FL_NET_ICMPV6_HDR_MIN)
        return 0;
    if (buf[0] != (uint8_t)FL_NET_ICMPV6_TYPE_ECHO_REPLY)
        return 0;
    if (fl_net_get_u16_be(buf + 4) != id)
        return 0;
    if (fl_net_get_u16_be(buf + 6) != seq)
        return 0;
    return 1;
}

fl_result_t fl_net_loopback_icmpv6_echo(const uint8_t *icmp_req, size_t icmp_len,
                                        const uint8_t *src6, const uint8_t *dst6,
                                        uint8_t *icmp_reply, size_t reply_cap,
                                        size_t *reply_len) {
    uint16_t csum;

    if (!icmp_req || icmp_len < FL_NET_ICMPV6_HDR_MIN || !icmp_reply || !reply_len || !src6 ||
        !dst6)
        return FL_RESULT_INVAL;
    if (reply_cap < icmp_len)
        return FL_RESULT_ERR;
    if (icmp_req[0] != (uint8_t)FL_NET_ICMPV6_TYPE_ECHO)
        return FL_RESULT_TIMEDOUT;

    memcpy(icmp_reply, icmp_req, icmp_len);
    icmp_reply[0] = (uint8_t)FL_NET_ICMPV6_TYPE_ECHO_REPLY;
    fl_net_put_u16_be(icmp_reply + 2, 0u);
    csum = fl_net_ipv6_icmp6_checksum(dst6, src6, icmp_reply, icmp_len);
    fl_net_put_u16_be(icmp_reply + 2, csum);
    *reply_len = icmp_len;
    return FL_RESULT_OK;
}
