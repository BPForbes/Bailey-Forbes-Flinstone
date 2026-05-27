#include "net_loopback.h"

#include "contract_p3_ipv4.h"
#include "contract_p3_wire.h"
#include "fl/net_asm.h"
#include "net_arp.h"
#include "net_checksum.h"
#include "net_ipv4.h"
#include "net_tcp.h"
#include "net_udp.h"
#include "net_wire.h"

#include <string.h>

#define FL_NET_LOOPBACK_RX_SLOTS 8u
#define FL_NET_LOOPBACK_RX_MAX FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
    size_t len;
    uint8_t data[FL_NET_LOOPBACK_RX_MAX];
} fl_net_loopback_rx_slot_t;

static fl_net_loopback_rx_slot_t s_lb_rx[FL_NET_LOOPBACK_RX_SLOTS];
static unsigned s_lb_rx_head;
static unsigned s_lb_rx_count;
static uint64_t s_lb_tx;
static uint64_t s_lb_stat_rx;

static void loopback_rx_clear(void) {
    s_lb_rx_head = 0;
    s_lb_rx_count = 0;
}

static fl_result_t loopback_rx_enqueue(const uint8_t *frame, size_t len) {
    unsigned idx;

    if (!frame || len == 0 || len > FL_NET_LOOPBACK_RX_MAX)
        return FL_RESULT_INVAL;
    if (s_lb_rx_count >= FL_NET_LOOPBACK_RX_SLOTS)
        return FL_RESULT_ERR;

    idx = (s_lb_rx_head + s_lb_rx_count) % FL_NET_LOOPBACK_RX_SLOTS;
    memcpy(s_lb_rx[idx].data, frame, len);
    s_lb_rx[idx].len = len;
    s_lb_rx_count++;
    return FL_RESULT_OK;
}

static fl_result_t loopback_rx_dequeue(uint8_t *buf, size_t cap, size_t *len) {
    if (!buf || !len)
        return FL_RESULT_INVAL;
    if (s_lb_rx_count == 0)
        return FL_RESULT_TIMEDOUT;

    if (cap < s_lb_rx[s_lb_rx_head].len)
        return FL_RESULT_ERR;

    *len = s_lb_rx[s_lb_rx_head].len;
    memcpy(buf, s_lb_rx[s_lb_rx_head].data, *len);
    s_lb_rx_head = (s_lb_rx_head + 1u) % FL_NET_LOOPBACK_RX_SLOTS;
    s_lb_rx_count--;
    return FL_RESULT_OK;
}

static size_t loopback_build_ipv4_reply(const uint8_t *req_ip, size_t req_ip_len,
                                        const void *payload, size_t payload_len,
                                        uint8_t *out_ip, size_t out_cap) {
    size_t hdr_len;
    uint16_t csum;

    if (!req_ip || req_ip_len < FL_NET_IPV4_HDR_LEN_MIN || !out_ip)
        return 0;
    hdr_len = (size_t)((req_ip[0] & 0x0fu) * 4u);
    if (hdr_len < FL_NET_IPV4_HDR_LEN_MIN || req_ip_len < hdr_len)
        return 0;
    if (out_cap < hdr_len + payload_len)
        return 0;

    memcpy(out_ip, req_ip, hdr_len);
    memcpy(out_ip + 12, req_ip + 16, 4u);
    memcpy(out_ip + 16, req_ip + 12, 4u);

    {
        uint16_t total = (uint16_t)(hdr_len + payload_len);
        out_ip[2] = (uint8_t)(total >> 8);
        out_ip[3] = (uint8_t)(total & 0xff);
    }
    if (payload && payload_len > 0)
        memcpy(out_ip + hdr_len, payload, payload_len);

    out_ip[10] = 0;
    out_ip[11] = 0;
    csum = fl_net_ipv4_checksum(out_ip, hdr_len);
    out_ip[10] = (uint8_t)(csum >> 8);
    out_ip[11] = (uint8_t)(csum & 0xff);
    return hdr_len + payload_len;
}

static fl_result_t loopback_process_arp(const uint8_t *frame, size_t len, uint8_t *eth_reply,
                                        size_t eth_cap, size_t *eth_reply_len) {
    uint8_t host_mac[6];
    uint32_t local_ip;

    if (!frame || !eth_reply || !eth_reply_len)
        return FL_RESULT_INVAL;

    fl_net_loopback_mac_host(host_mac);
    local_ip = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
    return fl_net_arp_input(frame, len, local_ip, host_mac, eth_reply, eth_cap, eth_reply_len);
}

static fl_result_t loopback_process_ipv4(const uint8_t *ip, size_t ip_len, uint8_t *eth_reply,
                                         size_t eth_cap, size_t *eth_reply_len) {
    size_t hdr_len;
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    uint8_t ip_reply[FL_NET_LOOPBACK_RX_MAX];
    size_t ip_reply_len;
    uint32_t dst_be;

    if (!ip || ip_len < FL_NET_IPV4_HDR_LEN_MIN || !eth_reply || !eth_reply_len)
        return FL_RESULT_INVAL;

    hdr_len = (size_t)((ip[0] & 0x0fu) * 4u);
    if (hdr_len < FL_NET_IPV4_HDR_LEN_MIN || ip_len < hdr_len)
        return FL_RESULT_INVAL;

    dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) | ((uint32_t)ip[18] << 16) |
             ((uint32_t)ip[19] << 24);
    if (!fl_net_ipv4_is_loopback(dst_be))
        return FL_RESULT_TIMEDOUT;

    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);

    if (ip[9] == FL_NET_IP_PROTO_ICMP) {
        const uint8_t *icmp = ip + hdr_len;
        size_t icmp_len = ip_len - hdr_len;
        uint8_t icmp_reply[FL_NET_LOOPBACK_RX_MAX];
        size_t icmp_reply_len = 0;

        if (fl_net_loopback_icmp_echo(icmp, icmp_len, icmp_reply, sizeof(icmp_reply),
                                      &icmp_reply_len) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        ip_reply_len = loopback_build_ipv4_reply(ip, ip_len, icmp_reply, icmp_reply_len, ip_reply,
                                                 sizeof(ip_reply));
    } else if (ip[9] == FL_NET_IP_PROTO_TCP) {
        const uint8_t *tcp = ip + hdr_len;
        size_t tcp_len = ip_len - hdr_len;
        uint8_t tcp_reply[64];
        size_t tcp_reply_len = 0;
        uint16_t sport = 0;
        uint16_t dport = 0;

        if (tcp_len < 4)
            return FL_RESULT_ERR;
#if defined(FL_NET_ASM_AVAILABLE)
        if (asm_net_tcp_read_ports_be(tcp, tcp_len, &sport, &dport) != 0)
            return FL_RESULT_ERR;
#else
        sport = (uint16_t)(((uint16_t)tcp[0] << 8) | tcp[1]);
        dport = (uint16_t)(((uint16_t)tcp[2] << 8) | tcp[3]);
#endif

        if (fl_net_loopback_tcp_syn(tcp, tcp_len, sport, dport, tcp_reply, sizeof(tcp_reply),
                                    &tcp_reply_len) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        ip_reply_len = loopback_build_ipv4_reply(ip, ip_len, tcp_reply, tcp_reply_len, ip_reply,
                                                 sizeof(ip_reply));
    } else if (ip[9] == FL_NET_IP_PROTO_UDP) {
        const uint8_t *udp = ip + hdr_len;
        size_t udp_len = ip_len - hdr_len;
        uint16_t sport = 0;
        uint16_t dport = 0;
        uint32_t src_be;
        size_t payload_len;

        if (udp_len < FL_NET_UDP_HDR_LEN)
            return FL_RESULT_ERR;
        sport = (uint16_t)(((uint16_t)udp[0] << 8) | udp[1]);
        dport = (uint16_t)(((uint16_t)udp[2] << 8) | udp[3]);
        payload_len = udp_len - FL_NET_UDP_HDR_LEN;
        src_be = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) | ((uint32_t)ip[14] << 16) |
                 ((uint32_t)ip[15] << 24);

        if (fl_net_udp_deliver_inbound(src_be, sport, dport, udp + FL_NET_UDP_HDR_LEN,
                                       payload_len) == FL_RESULT_OK)
            return FL_RESULT_TIMEDOUT;
        return FL_RESULT_TIMEDOUT;
    } else {
        return FL_RESULT_TIMEDOUT;
    }

    if (ip_reply_len == 0)
        return FL_RESULT_ERR;

    *eth_reply_len = fl_net_wire_build_eth_ipv4(eth_reply, eth_cap, host_mac, peer_mac, ip_reply,
                                                ip_reply_len);
    return (*eth_reply_len > 0) ? FL_RESULT_OK : FL_RESULT_ERR;
}

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

fl_result_t fl_net_loopback_tcp_syn(const uint8_t *tcp_syn, size_t tcp_len, uint16_t sport,
                                    uint16_t dport, uint8_t *tcp_reply, size_t reply_cap,
                                    size_t *reply_len) {
    uint16_t pkt_sport = 0;
    uint16_t pkt_dport = 0;
    size_t built;

    if (!tcp_syn || tcp_len < FL_NET_TCP_HDR_LEN_MIN || !tcp_reply || !reply_len)
        return FL_RESULT_INVAL;
    if (reply_cap < FL_NET_TCP_HDR_LEN_MIN)
        return FL_RESULT_ERR;

#if defined(FL_NET_ASM_AVAILABLE)
    if (asm_net_tcp_read_ports_be(tcp_syn, tcp_len, &pkt_sport, &pkt_dport) != 0)
        return FL_RESULT_ERR;
    if (pkt_sport != sport || pkt_dport != dport)
        return FL_RESULT_INVAL;
    built = asm_net_tcp_build_rst_ack(tcp_syn, tcp_len, tcp_reply, reply_cap);
#else
    {
        uint32_t seq;

        pkt_sport = (uint16_t)(((uint16_t)tcp_syn[0] << 8) | tcp_syn[1]);
        pkt_dport = (uint16_t)(((uint16_t)tcp_syn[2] << 8) | tcp_syn[3]);
        if (pkt_sport != sport || pkt_dport != dport)
            return FL_RESULT_INVAL;
        memset(tcp_reply, 0, FL_NET_TCP_HDR_LEN_MIN);
        seq = ((uint32_t)tcp_syn[4] << 24) | ((uint32_t)tcp_syn[5] << 16) |
              ((uint32_t)tcp_syn[6] << 8) | (uint32_t)tcp_syn[7];
        if (tcp_syn[13] & FL_NET_TCP_FLAG_SYN)
            seq++;
        tcp_reply[0] = tcp_syn[2];
        tcp_reply[1] = tcp_syn[3];
        tcp_reply[2] = tcp_syn[0];
        tcp_reply[3] = tcp_syn[1];
        tcp_reply[8] = (uint8_t)((seq >> 24) & 0xff);
        tcp_reply[9] = (uint8_t)((seq >> 16) & 0xff);
        tcp_reply[10] = (uint8_t)((seq >> 8) & 0xff);
        tcp_reply[11] = (uint8_t)(seq & 0xff);
        tcp_reply[12] = (uint8_t)(5u << 4);
        tcp_reply[13] = (uint8_t)(FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK);
        built = FL_NET_TCP_HDR_LEN_MIN;
    }
#endif

    if (built != FL_NET_TCP_HDR_LEN_MIN)
        return FL_RESULT_ERR;
    *reply_len = built;
    return FL_RESULT_OK;
}

fl_result_t fl_net_loopback_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
    size_t ip_off;
    size_t ip_len;
    uint32_t dst_be;
    uint8_t reply[FL_NET_LOOPBACK_RX_MAX];
    size_t reply_len = 0;

    (void)drv;

    if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    s_lb_tx++;

    {
        int eth_ok = 0;
        uint16_t ethertype = fl_net_wire_ethertype_be16(frame->data, frame->len, &eth_ok);
        if (eth_ok && ethertype == FL_ETHERTYPE_ARP) {
            if (loopback_process_arp(frame->data, frame->len, reply, sizeof(reply), &reply_len) !=
                FL_RESULT_OK)
                return FL_RESULT_OK;
            return loopback_rx_enqueue(reply, reply_len);
        }
    }

    if (!fl_net_wire_parse_eth_ipv4(frame->data, frame->len, &ip_off, &ip_len, &dst_be))
        return FL_RESULT_OK;

    if (loopback_process_ipv4(frame->data + ip_off, ip_len, reply, sizeof(reply), &reply_len) !=
        FL_RESULT_OK)
        return FL_RESULT_OK;

    return loopback_rx_enqueue(reply, reply_len);
}

fl_result_t fl_net_loopback_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    fl_result_t rc;

    (void)drv;

    if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    rc = loopback_rx_dequeue(out->data, out->cap, &out->len);
    if (rc == FL_RESULT_OK)
        (void)fl_net_wire_check_rx_fill(out, out->len);
    if (rc == FL_RESULT_OK)
        s_lb_stat_rx++;
    return rc;
}

uint64_t fl_net_loopback_stat_tx(void) {
    return s_lb_tx;
}

uint64_t fl_net_loopback_stat_rx(void) {
    return s_lb_stat_rx;
}

void fl_net_loopback_reset(void) {
    loopback_rx_clear();
    s_lb_tx = 0;
    s_lb_stat_rx = 0;
}
