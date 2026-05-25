#include "net_wire_host.h"

#include "contract_p3_ipv4.h"
#include "net_eth.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_tcp.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
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

static fl_result_t wire_loopback_exchange(const uint8_t *l4, size_t l4_len, uint8_t ip_proto,
                                          uint32_t dst_be, uint8_t *rx_l4, size_t rx_l4_cap,
                                          size_t *rx_l4_len, unsigned timeout_ms,
                                          double *out_rtt_ms) {
    uint8_t ipbuf[576];
    uint8_t frame[FL_NET_ETH_HDR_LEN + 576];
    uint8_t rx_frame[FL_NET_ETH_HDR_LEN + 576];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    fl_net_ipv4_hdr_t hdr;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    size_t ip_len;
    size_t frame_len;
    size_t ip_off;
    size_t ip_len_rx;
    uint32_t dummy_dst;
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
    frame_len = fl_net_eth_build_ipv4(frame, sizeof(frame), peer_mac, host_mac, ipbuf, ip_len);
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

    if (!fl_net_eth_parse_ipv4(rx_frame, mut.len, &ip_off, &ip_len_rx, &dummy_dst))
        return FL_RESULT_ERR;
    if (ip_len_rx <= (size_t)((rx_frame[ip_off] & 0x0fu) * 4u) ||
        ip_len_rx - (size_t)((rx_frame[ip_off] & 0x0fu) * 4u) > rx_l4_cap)
        return FL_RESULT_ERR;

    {
        size_t hdr_len = (size_t)((rx_frame[ip_off] & 0x0fu) * 4u);
        size_t payload = ip_len_rx - hdr_len;
        memcpy(rx_l4, rx_frame + ip_off + hdr_len, payload);
        *rx_l4_len = payload;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_wire_send_icmp(uint32_t dst_be, const uint8_t *icmp, size_t icmp_len,
                                  uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                  unsigned timeout_ms, double *out_rtt_ms) {
    struct timeval t0, t1;

    if (!icmp || icmp_len < 8 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    gettimeofday(&t0, NULL);

    if (fl_net_loopback_owns(dst_be)) {
        return wire_loopback_exchange(icmp, icmp_len, FL_NET_IP_PROTO_ICMP, dst_be, rx, rx_cap,
                                      rx_len, timeout_ms, out_rtt_ms);
    }

#if defined(__linux__)
    {
        int sock;
        struct sockaddr_in dst;
        struct timeval tv;
        ssize_t n;

        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = dst_be;

        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
        if (sock < 0)
            return (errno == EACCES) ? FL_RESULT_ACCES : FL_RESULT_ERR;

        tv.tv_sec = (time_t)(timeout_ms / 1000u);
        tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
        (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (sendto(sock, icmp, icmp_len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
            close(sock);
            return FL_RESULT_ERR;
        }

        n = recvfrom(sock, rx, rx_cap, 0, NULL, NULL);
        close(sock);
        gettimeofday(&t1, NULL);
        if (n < 0)
            return (errno == EAGAIN || errno == EWOULDBLOCK) ? FL_RESULT_TIMEDOUT
                                                             : FL_RESULT_ERR;
        *rx_len = (size_t)n;
        if (out_rtt_ms)
            *out_rtt_ms = timeval_delta_ms(&t0, &t1);
        return FL_RESULT_OK;
    }
#else
    (void)timeout_ms;
    (void)rx_cap;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wire_send_tcp_syn(uint32_t dst_be, uint16_t sport_unused, uint16_t dport,
                                     const uint8_t *tcp, size_t tcp_len,
                                     unsigned timeout_ms, double *out_rtt_ms, char *note,
                                     size_t note_len) {
    struct timeval t0, t1;

    if (!tcp || tcp_len < FL_NET_TCP_HDR_LEN_MIN)
        return FL_RESULT_INVAL;

    (void)sport_unused;

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
        int sock;
        struct sockaddr_in dst;
        struct timeval tv;
        fd_set rfds;
        uint8_t rx[128];
        ssize_t n;
        int flags;
        int so;

        sock = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
        if (sock < 0) {
            if (note && note_len > 0)
                snprintf(note, note_len, "raw tcp socket: %s", strerror(errno));
            return (errno == EPERM || errno == EACCES) ? FL_RESULT_ACCES : FL_RESULT_ERR;
        }

        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = dst_be;
        dst.sin_port = htons(dport);

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
        if (so <= 0) {
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

        if (note && note_len > 0) {
            size_t ip_hdr_len = (size_t)((rx[0] & 0x0fu) * 4u);
            const uint8_t *tcp = rx + ip_hdr_len;
            size_t tcp_len = (size_t)n - ip_hdr_len;

            if (tcp_len < FL_NET_TCP_HDR_LEN_MIN) {
                snprintf(note, note_len, "tcp reply too short");
            } else if (tcp[13] & (FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK))
                snprintf(note, note_len, "tcp rst+ack (wire)");
            else if ((tcp[13] & FL_NET_TCP_FLAG_SYN) && (tcp[13] & FL_NET_TCP_FLAG_ACK))
                snprintf(note, note_len, "tcp syn+ack (wire)");
            else
                snprintf(note, note_len, "tcp flags 0x%02x", (unsigned)tcp[13]);
        }
        return FL_RESULT_OK;
    }
#else
    (void)sport;
    (void)dport;
    (void)timeout_ms;
    (void)note;
    (void)note_len;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wire_send_udp(uint32_t dst_be, uint16_t sport_unused, uint16_t dport,
                                 const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                 size_t rx_cap, size_t *rx_len, unsigned timeout_ms) {
#if defined(__linux__)
    int sock;
    struct sockaddr_in dst;
    struct timeval tv;
    ssize_t n;

    if (!payload || payload_len == 0 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    (void)sport_unused;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return FL_RESULT_ERR;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = dst_be;
    dst.sin_port = htons(dport);

    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    if (sendto(sock, payload, payload_len, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        close(sock);
        return FL_RESULT_ERR;
    }

    n = recvfrom(sock, rx, rx_cap, 0, NULL, NULL);
    close(sock);
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
