#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "net_wire_host.h"

#include "contract_p3_ipv4.h"
#include "fl/net_asm.h"
#include "net_ipv4.h"
#include "net_wire.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_tcp.h"
#include "net_packet.h"
#include "net_udp.h"
#include "net_wire_egress.h"
#include "net_wire_host_syscall.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>

#if defined(__linux__)
#include <netinet/ip.h>
#endif

static double timeval_delta_ms(const struct timeval *a, const struct timeval *b) {
    return (double)(b->tv_sec - a->tv_sec) * 1000.0 +
           (double)(b->tv_usec - a->tv_usec) / 1000.0;
}

static uint32_t wire_loopback_src_be(void) {
    return (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
}

static void wire_host_sin4(struct sockaddr_in *sa, uint32_t addr_be, uint16_t port_host) {
    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;
    sa->sin_addr.s_addr = addr_be;
#if defined(FL_NET_ASM_AVAILABLE)
    sa->sin_port = asm_net_htons_be16(port_host);
#else
    sa->sin_port = htons(port_host);
#endif
}

static fl_result_t wire_loopback_exchange(const uint8_t *l4, size_t l4_len, uint8_t ip_proto,
                                          uint32_t dst_be, uint8_t *rx_l4, size_t rx_l4_cap,
                                          size_t *rx_l4_len, unsigned timeout_ms,
                                          double *out_rtt_ms) {
    uint8_t ipbuf[576];
    uint8_t frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t rx_frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    fl_net_ipv4_hdr_t hdr;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    size_t ip_len;
    size_t frame_len;
    struct timeval t0, t1;
    fl_result_t rc;

    if (!l4 || l4_len == 0 || !rx_l4 || !rx_l4_len)
        return FL_RESULT_INVAL;

    fl_net_loopback_reset();

    ip_len = fl_net_ipv4_build(&hdr, ipbuf, sizeof(ipbuf), ip_proto, wire_loopback_src_be(),
                               dst_be, l4, l4_len, 0x4242u);
    if (ip_len == 0)
        return FL_RESULT_ERR;

    fl_net_loopback_mac_peer(peer_mac);
    fl_net_loopback_mac_host(host_mac);
    frame_len = fl_net_wire_build_eth_ipv4(frame, sizeof(frame), peer_mac, host_mac, ipbuf, ip_len);
    if (frame_len == 0)
        return FL_RESULT_ERR;

    view.data = frame;
    view.len = frame_len;

    gettimeofday(&t0, NULL);
    rc = fl_net_netdev_send(fl_net_netdev_loopback(), &view);
    if (rc != FL_RESULT_OK)
        return rc;

    mut.data = rx_frame;
    mut.cap = sizeof(rx_frame);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, timeout_ms);
    gettimeofday(&t1, NULL);
    if (rc != FL_RESULT_OK)
        return rc;
    if (out_rtt_ms)
        *out_rtt_ms = timeval_delta_ms(&t0, &t1);

    {
        fl_net_packet_t rx_pkt;
        fl_result_t prc;

        prc = fl_net_packet_parse_eth_ipv4(rx_frame, mut.len, &rx_pkt);
        if (prc != FL_RESULT_OK)
            return prc;
        return fl_net_packet_copy_l4(&rx_pkt, rx_l4, rx_l4_cap, rx_l4_len);
    }
}

fl_result_t fl_net_wire_send_icmp_pkt(uint32_t dst_be, const fl_net_packet_t *icmp_pkt,
                                      fl_net_packet_t *rx_pkt, uint8_t *rx_backing,
                                      size_t rx_backing_cap, size_t *rx_len,
                                      unsigned timeout_ms, double *out_rtt_ms) {
    const uint8_t *icmp;
    size_t icmp_len;
    fl_result_t rc;

    if (!icmp_pkt || !rx_pkt || !rx_backing || !rx_len)
        return FL_RESULT_INVAL;

    rc = fl_net_packet_l4_view(icmp_pkt, &icmp, &icmp_len);
    if (rc != FL_RESULT_OK || icmp_len < 8u)
        return FL_RESULT_INVAL;

    rc = fl_net_wire_egress_l4_pkt(dst_be, FL_NET_IP_PROTO_ICMP, icmp_pkt, rx_backing,
                                   rx_backing_cap, rx_len, timeout_ms, out_rtt_ms);
    if (rc == FL_RESULT_OK) {
        fl_result_t bind_rc =
            fl_net_packet_bind_l4(rx_pkt, rx_backing, rx_backing_cap, 0u, *rx_len);
        return bind_rc;
    }

    if (fl_net_loopback_owns(dst_be)) {
        fl_result_t bind_rc;

        rc = wire_loopback_exchange(icmp, icmp_len, FL_NET_IP_PROTO_ICMP, dst_be, rx_backing,
                                    rx_backing_cap, rx_len, timeout_ms, out_rtt_ms);
        if (rc != FL_RESULT_OK)
            return rc;
        bind_rc = fl_net_packet_bind_l4(rx_pkt, rx_backing, rx_backing_cap, 0u, *rx_len);
        return bind_rc;
    }

#if defined(__linux__)
    {
        struct sockaddr_in dst;
        struct timeval t0, t1;
        struct timeval tv;
        ssize_t n;
        int sock;

        wire_host_sin4(&dst, dst_be, 0);

        sock = net_host_socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (sock < 0)
            return (errno == EACCES) ? FL_RESULT_ACCES : FL_RESULT_ERR;

        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        (void)net_host_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, (unsigned int)sizeof(tv));

        gettimeofday(&t0, NULL);
        if (net_host_sendto(sock, icmp, icmp_len, 0, &dst, (unsigned int)sizeof(dst)) < 0) {
            net_host_close(sock);
            return FL_RESULT_ERR;
        }

        n = net_host_recvfrom(sock, rx_backing, rx_backing_cap, 0, NULL, NULL);
        net_host_close(sock);
        gettimeofday(&t1, NULL);
        if (n < 0)
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? FL_RESULT_TIMEDOUT
                                                             : FL_RESULT_ERR;
        *rx_len = (size_t)n;
        rc = fl_net_packet_bind_l4(rx_pkt, rx_backing, rx_backing_cap, 0u, *rx_len);
        if (rc != FL_RESULT_OK)
            return rc;
        if (out_rtt_ms)
            *out_rtt_ms = timeval_delta_ms(&t0, &t1);
        return FL_RESULT_OK;
    }
#else
    (void)timeout_ms;
    (void)out_rtt_ms;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wire_send_icmp(uint32_t dst_be, const uint8_t *icmp, size_t icmp_len,
                                  uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                  unsigned timeout_ms, double *out_rtt_ms) {
    fl_net_packet_t req_pkt;
    fl_net_packet_t rx_pkt;
    fl_result_t rc;

    if (!icmp || icmp_len < 8 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    rc = fl_net_packet_bind_l4(&req_pkt, (uint8_t *)(uintptr_t)icmp, icmp_len, 0u, icmp_len);
    if (rc != FL_RESULT_OK)
        return rc;

    return fl_net_wire_send_icmp_pkt(dst_be, &req_pkt, &rx_pkt, rx, rx_cap, rx_len, timeout_ms,
                                     out_rtt_ms);
}

fl_result_t fl_net_wire_send_tcp_syn(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                     const uint8_t *tcp, size_t tcp_len,
                                     unsigned timeout_ms, double *out_rtt_ms, char *note,
                                     size_t note_len) {
    struct timeval t0, t1;
    uint16_t pkt_sport;

    if (!tcp || tcp_len < FL_NET_TCP_HDR_LEN_MIN)
        return FL_RESULT_INVAL;

    pkt_sport = (uint16_t)(((uint16_t)tcp[0] << 8) | tcp[1]);
    if (pkt_sport != sport)
        return FL_RESULT_INVAL;

    gettimeofday(&t0, NULL);

    if (fl_net_loopback_owns(dst_be)) {
        uint8_t reply[64];
        size_t reply_len = 0;
        fl_result_t rc = wire_loopback_exchange(tcp, tcp_len, FL_NET_IP_PROTO_TCP, dst_be, reply,
                                              sizeof(reply), &reply_len, timeout_ms, out_rtt_ms);
        if (rc == FL_RESULT_OK) {
            if (note && note_len > 0)
                snprintf(note, note_len, "loopback tcp rst+ack (P3-2 netdev)");
            return FL_RESULT_OK;
        }
        return rc;
    }

#if defined(__linux__)
    {
        struct sockaddr_in dst;
        struct timeval tv;
        fd_set rfds;
        uint8_t rx[128];
        ssize_t n;
        int flags;
        int so;
        int sock;

        sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
        if (sock < 0) {
            if (note && note_len > 0)
                snprintf(note, note_len, "raw tcp socket: %s", strerror(errno));
            return (errno == EPERM || errno == EACCES) ? FL_RESULT_ACCES : FL_RESULT_ERR;
        }

        wire_host_sin4(&dst, dst_be, dport);

        flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0)
            (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        if (sendto(sock, tcp, tcp_len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            close(sock);
            if (note && note_len > 0)
                snprintf(note, note_len, "tcp syn send: %s", strerror(errno));
            return FL_RESULT_ERR;
        }

        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        so = select(sock + 1, &rfds, NULL, NULL, &tv);
        gettimeofday(&t1, NULL);
        if (so < 0) {
            close(sock);
            if (note && note_len > 0)
                snprintf(note, note_len, "tcp syn wait: %s", strerror(errno));
            return FL_RESULT_ERR;
        }
        if (so == 0) {
            close(sock);
            if (note && note_len > 0)
                snprintf(note, note_len, "tcp syn timeout");
            return FL_RESULT_TIMEDOUT;
        }

        n = recv(sock, rx, sizeof(rx), 0);
        close(sock);
        if (n < (ssize_t)(FL_NET_IPV4_HDR_LEN_MIN + FL_NET_TCP_HDR_LEN_MIN))
            return FL_RESULT_ERR;

        if (out_rtt_ms)
            *out_rtt_ms = timeval_delta_ms(&t0, &t1);

        {
            size_t ip_hdr_len = (size_t)((rx[0] & 0x0FU) * 4U);
            const uint8_t *rx_tcp;
            size_t rx_tcp_len;
            uint32_t rx_src_be;
            uint32_t syn_seq;
            uint32_t rx_ack;
            uint16_t rx_sport;
            uint16_t rx_dport;

            if (ip_hdr_len < FL_NET_IPV4_HDR_LEN_MIN || ip_hdr_len > (size_t)n)
                return FL_RESULT_ERR;

            rx_tcp = rx + ip_hdr_len;
            rx_tcp_len = (size_t)n - ip_hdr_len;
            if (rx_tcp_len < FL_NET_TCP_HDR_LEN_MIN)
                return FL_RESULT_ERR;

            rx_src_be = (uint32_t)rx[12] | ((uint32_t)rx[13] << 8) | ((uint32_t)rx[14] << 16) |
                        ((uint32_t)rx[15] << 24);
            if (rx_src_be != dst_be)
                return FL_RESULT_ERR;

            rx_sport = (uint16_t)(((uint16_t)rx_tcp[0] << 8) | rx_tcp[1]);
            rx_dport = (uint16_t)(((uint16_t)rx_tcp[2] << 8) | rx_tcp[3]);
            if (rx_sport != dport || rx_dport != sport)
                return FL_RESULT_ERR;

            syn_seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                      ((uint32_t)tcp[6] << 8) | (uint32_t)tcp[7];
            rx_ack = ((uint32_t)rx_tcp[8] << 24) | ((uint32_t)rx_tcp[9] << 16) |
                     ((uint32_t)rx_tcp[10] << 8) | (uint32_t)rx_tcp[11];
            if ((rx_tcp[13] & (FL_NET_TCP_FLAG_SYN | FL_NET_TCP_FLAG_ACK)) ==
                    (FL_NET_TCP_FLAG_SYN | FL_NET_TCP_FLAG_ACK) &&
                rx_ack != syn_seq + 1u)
                return FL_RESULT_ERR;

            if (note && note_len > 0) {
                if (rx_tcp[13] & (FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK)) {
                    snprintf(note, note_len, "tcp rst+ack (wire)");
                } else if ((rx_tcp[13] & FL_NET_TCP_FLAG_SYN) &&
                           (rx_tcp[13] & FL_NET_TCP_FLAG_ACK)) {
                    snprintf(note, note_len, "tcp syn+ack (wire)");
                } else {
                    snprintf(note, note_len, "tcp flags 0x%02x", (unsigned)rx_tcp[13]);
                }
            }
        }
        return FL_RESULT_OK;
    }
#else
    (void)dst_be;
    (void)sport;
    (void)dport;
    (void)tcp;
    (void)tcp_len;
    (void)timeout_ms;
    (void)note;
    (void)note_len;
    return FL_RESULT_NOSYS;
#endif
}

static fl_result_t wire_host_send_udp_buf(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                          const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                          size_t rx_cap, size_t *rx_len, unsigned timeout_ms) {
#if defined(__linux__)
    struct sockaddr_in local;
    struct sockaddr_in dst;
    struct timeval tv;
    ssize_t n;
    int sock;

    if (!payload || payload_len == 0 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    wire_host_sin4(&dst, dst_be, dport);

    sock = net_host_socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return FL_RESULT_ERR;

    if (sport != 0u) {
        wire_host_sin4(&local, 0, sport);
        if (net_host_bind(sock, &local, (unsigned int)sizeof(local)) < 0) {
            net_host_close(sock);
            return FL_RESULT_ERR;
        }
    }

    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    (void)net_host_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, (unsigned int)sizeof(tv));

    if (net_host_sendto(sock, payload, payload_len, 0, &dst, (unsigned int)sizeof(dst)) < 0) {
        net_host_close(sock);
        return FL_RESULT_ERR;
    }

    n = net_host_recvfrom(sock, rx, rx_cap, 0, NULL, NULL);
    net_host_close(sock);
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? FL_RESULT_TIMEDOUT : FL_RESULT_ERR;
    *rx_len = (size_t)n;
    return FL_RESULT_OK;
#else
    (void)dst_be;
    (void)sport;
    (void)dport;
    (void)payload;
    (void)payload_len;
    (void)rx;
    (void)rx_cap;
    (void)rx_len;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wire_send_udp_pkt(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                     const fl_net_packet_t *payload_pkt, fl_net_packet_t *rx_pkt,
                                     uint8_t *rx_backing, size_t rx_backing_cap, size_t *rx_len,
                                     unsigned timeout_ms) {
    const uint8_t *payload;
    size_t payload_len;
    fl_result_t rc;

    if (!payload_pkt || !rx_pkt || !rx_backing || !rx_len)
        return FL_RESULT_INVAL;

    rc = fl_net_packet_l4_view(payload_pkt, &payload, &payload_len);
    if (rc != FL_RESULT_OK || payload_len == 0u)
        return FL_RESULT_INVAL;

    rc = wire_host_send_udp_buf(dst_be, sport, dport, payload, payload_len, rx_backing,
                                rx_backing_cap, rx_len, timeout_ms);
    if (rc == FL_RESULT_OK) {
        fl_result_t bind_rc =
            fl_net_packet_bind_l4(rx_pkt, rx_backing, rx_backing_cap, 0u, *rx_len);
        if (bind_rc != FL_RESULT_OK)
            return bind_rc;
    }
    return rc;
}

fl_result_t fl_net_wire_send_udp(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                 const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                 size_t rx_cap, size_t *rx_len, unsigned timeout_ms) {
    fl_net_packet_t tx_pkt;
    fl_net_packet_t rx_pkt;

    if (!payload || payload_len == 0 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    if (fl_net_packet_bind_l4(&tx_pkt, (uint8_t *)(uintptr_t)payload, payload_len, 0u,
                              payload_len) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    return fl_net_wire_send_udp_pkt(dst_be, sport, dport, &tx_pkt, &rx_pkt, rx, rx_cap, rx_len,
                                    timeout_ms);
}
