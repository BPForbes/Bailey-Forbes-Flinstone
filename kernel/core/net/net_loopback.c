#include "net_loopback.h"

#include "net_checksum.h"
#include "net_ipv4.h"
#include "net_tcp.h"

#include <string.h>

int fl_net_loopback_owns(uint32_t dst_be) {
    return fl_net_ipv4_is_loopback(dst_be);
}

fl_result_t fl_net_loopback_icmp_echo(const uint8_t *icmp_req, size_t icmp_len,
                                      uint8_t *icmp_reply, size_t reply_cap,
                                      size_t *reply_len) {
    size_t need;

    if (!icmp_req || icmp_len < FL_NET_ICMPV4_HDR_MIN || !icmp_reply || !reply_len)
        return FL_RESULT_INVAL;

    need = icmp_len;
    if (reply_cap < need)
        return FL_RESULT_ERR;

    memcpy(icmp_reply, icmp_req, icmp_len);
    icmp_reply[0] = (uint8_t)FL_NET_ICMPV4_TYPE_ECHO_REPLY;
    icmp_reply[2] = 0;
    icmp_reply[3] = 0;
    {
        uint16_t csum = fl_net_checksum16(icmp_reply, icmp_len);
        icmp_reply[2] = (uint8_t)(csum >> 8);
        icmp_reply[3] = (uint8_t)(csum & 0xff);
    }
    *reply_len = need;
    return FL_RESULT_OK;
}

fl_result_t fl_net_loopback_tcp_syn(const uint8_t *tcp_syn, size_t tcp_len, uint16_t dport,
                                    uint8_t *tcp_reply, size_t reply_cap, size_t *reply_len) {
    uint32_t seq;
    uint16_t sport;
    uint16_t csum;

    (void)dport;

    if (!tcp_syn || tcp_len < FL_NET_TCP_HDR_LEN_MIN || !tcp_reply || !reply_len)
        return FL_RESULT_INVAL;
    if (reply_cap < FL_NET_TCP_HDR_LEN_MIN)
        return FL_RESULT_ERR;

    memset(tcp_reply, 0, FL_NET_TCP_HDR_LEN_MIN);
    sport = (uint16_t)((tcp_syn[0] << 8) | tcp_syn[1]);
    seq = ((uint32_t)tcp_syn[4] << 24) | ((uint32_t)tcp_syn[5] << 16) |
          ((uint32_t)tcp_syn[6] << 8) | (uint32_t)tcp_syn[7];

    tcp_reply[0] = tcp_syn[2];
    tcp_reply[1] = tcp_syn[3];
    tcp_reply[2] = tcp_syn[0];
    tcp_reply[3] = tcp_syn[1];
    tcp_reply[4] = 0;
    tcp_reply[5] = 0;
    tcp_reply[6] = 0;
    tcp_reply[7] = 0;
    tcp_reply[8] = (uint8_t)((seq >> 24) & 0xff);
    tcp_reply[9] = (uint8_t)((seq >> 16) & 0xff);
    tcp_reply[10] = (uint8_t)((seq >> 8) & 0xff);
    tcp_reply[11] = (uint8_t)(seq & 0xff);
    tcp_reply[12] = (uint8_t)(5u << 4);
    tcp_reply[13] = (uint8_t)(FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK);
    tcp_reply[14] = 0;
    tcp_reply[15] = 0;

    csum = 0;
    tcp_reply[16] = 0;
    tcp_reply[17] = 0;
    (void)sport;
    (void)csum;
    *reply_len = FL_NET_TCP_HDR_LEN_MIN;
    return FL_RESULT_OK;
}
