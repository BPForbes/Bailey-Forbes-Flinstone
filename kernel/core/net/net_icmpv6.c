#include "net_icmpv6.h"

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
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = (uint8_t)(id >> 8);
    buf[5] = (uint8_t)(id & 0xff);
    buf[6] = (uint8_t)(seq >> 8);
    buf[7] = (uint8_t)(seq & 0xff);
    if (payload_len > 0)
        memset(buf + FL_NET_ICMPV6_HDR_MIN, 0x5a, payload_len);

    fl_net_ipv6_loopback_addr(lb);
    csum = fl_net_ipv6_icmp6_checksum(lb, lb, buf, need);
    buf[2] = (uint8_t)(csum >> 8);
    buf[3] = (uint8_t)(csum & 0xff);
    return need;
}

int fl_net_icmpv6_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq) {
    if (!buf || len < FL_NET_ICMPV6_HDR_MIN)
        return 0;
    if (buf[0] != (uint8_t)FL_NET_ICMPV6_TYPE_ECHO_REPLY)
        return 0;
    if (((uint16_t)buf[4] << 8 | buf[5]) != id)
        return 0;
    if (((uint16_t)buf[6] << 8 | buf[7]) != seq)
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
    icmp_reply[2] = 0;
    icmp_reply[3] = 0;
    csum = fl_net_ipv6_icmp6_checksum(dst6, src6, icmp_reply, icmp_len);
    icmp_reply[2] = (uint8_t)(csum >> 8);
    icmp_reply[3] = (uint8_t)(csum & 0xff);
    *reply_len = icmp_len;
    return FL_RESULT_OK;
}
