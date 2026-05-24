#include "net_icmp.h"

#include "net_checksum.h"
#include "net_ipv4.h"
#include "net_wire_host.h"

#include <string.h>

size_t fl_net_icmp_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                      size_t payload_len) {
    size_t need = FL_NET_ICMPV4_HDR_MIN + payload_len;
    uint16_t csum;

    if (!buf || cap < need)
        return 0;

    buf[0] = (uint8_t)FL_NET_ICMPV4_TYPE_ECHO;
    buf[1] = 0;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = (uint8_t)(id >> 8);
    buf[5] = (uint8_t)(id & 0xff);
    buf[6] = (uint8_t)(seq >> 8);
    buf[7] = (uint8_t)(seq & 0xff);
    if (payload_len > 0)
        memset(buf + FL_NET_ICMPV4_HDR_MIN, 0x5a, payload_len);

    csum = fl_net_checksum16(buf, need);
    buf[2] = (uint8_t)(csum >> 8);
    buf[3] = (uint8_t)(csum & 0xff);
    return need;
}

int fl_net_icmp_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq) {
    if (!buf || len < FL_NET_ICMPV4_HDR_MIN)
        return 0;
    if (buf[0] != (uint8_t)FL_NET_ICMPV4_TYPE_ECHO_REPLY)
        return 0;
    if (((uint16_t)buf[4] << 8 | buf[5]) != id)
        return 0;
    if (((uint16_t)buf[6] << 8 | buf[7]) != seq)
        return 0;
    return 1;
}

fl_result_t fl_net_icmp_echo_exchange(uint32_t dst_be, uint16_t id, uint16_t seq,
                                      unsigned timeout_ms, double *out_rtt_ms) {
    uint8_t req[FL_NET_ICMPV4_HDR_MIN + FL_NET_ICMP_ECHO_PAYLOAD_DEFAULT];
    uint8_t rx[512];
    size_t req_len;
    size_t rx_len;
    fl_result_t rc;

    req_len = fl_net_icmp_echo_request_build(req, sizeof(req), id, seq,
                                             FL_NET_ICMP_ECHO_PAYLOAD_DEFAULT);
    if (req_len == 0)
        return FL_RESULT_ERR;

    rc = fl_net_wire_send_icmp(dst_be, req, req_len, rx, sizeof(rx), &rx_len, timeout_ms,
                               out_rtt_ms);
    if (rc != FL_RESULT_OK)
        return rc;
    if (!fl_net_icmp_echo_reply_match(rx, rx_len, id, seq))
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}
