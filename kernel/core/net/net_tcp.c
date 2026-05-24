#include "net_tcp.h"

#include "net_checksum.h"
#include "net_ipv4.h"
#include "net_wire_host.h"

#include <stdio.h>
#include <string.h>

size_t fl_net_tcp_build_syn(uint8_t *buf, size_t cap, uint16_t sport, uint16_t dport,
                            uint32_t seq_be) {
    if (!buf || cap < FL_NET_TCP_HDR_LEN_MIN)
        return 0;

    memset(buf, 0, FL_NET_TCP_HDR_LEN_MIN);
    buf[0] = (uint8_t)(sport >> 8);
    buf[1] = (uint8_t)(sport & 0xff);
    buf[2] = (uint8_t)(dport >> 8);
    buf[3] = (uint8_t)(dport & 0xff);
    buf[4] = (uint8_t)(seq_be >> 24);
    buf[5] = (uint8_t)(seq_be >> 16);
    buf[6] = (uint8_t)(seq_be >> 8);
    buf[7] = (uint8_t)(seq_be);
    buf[8] = 0;
    buf[9] = 0;
    buf[10] = 0;
    buf[11] = 0;
    buf[12] = (uint8_t)(5u << 4);
    buf[13] = FL_NET_TCP_FLAG_SYN;
    buf[14] = 0x20;
    buf[15] = 0x00;
    return FL_NET_TCP_HDR_LEN_MIN;
}

fl_result_t fl_net_tcp_syn_probe(uint32_t dst_be, uint16_t dport, unsigned timeout_ms,
                                 double *out_rtt_ms, char *note, size_t note_len) {
    uint8_t tcp[FL_NET_TCP_HDR_LEN_MIN];
    size_t tcp_len;
    uint16_t sport = 40000u + (uint16_t)(dport & 0xffu);

    tcp_len = fl_net_tcp_build_syn(tcp, sizeof(tcp), sport, dport, 1u);
    if (tcp_len == 0)
        return FL_RESULT_ERR;

    return fl_net_wire_send_tcp_syn(dst_be, sport, dport, tcp, tcp_len, timeout_ms,
                                    out_rtt_ms, note, note_len);
}
