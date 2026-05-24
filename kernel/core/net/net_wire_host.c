#include "net_wire_host.h"

#include "net_ipv4.h"
#include "net_loopback.h"
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

fl_result_t fl_net_wire_send_icmp(uint32_t dst_be, const uint8_t *icmp, size_t icmp_len,
                                  uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                  unsigned timeout_ms, double *out_rtt_ms) {
    struct timeval t0, t1;

    if (!icmp || icmp_len < 8 || !rx || !rx_len)
        return FL_RESULT_INVAL;

    gettimeofday(&t0, NULL);

    if (fl_net_loopback_owns(dst_be)) {
        fl_result_t rc = fl_net_loopback_icmp_echo(icmp, icmp_len, rx, rx_cap, rx_len);
        gettimeofday(&t1, NULL);
        if (rc == FL_RESULT_OK && out_rtt_ms)
            *out_rtt_ms = timeval_delta_ms(&t0, &t1);
        return rc;
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
        fl_result_t rc =
            fl_net_loopback_tcp_syn(tcp, tcp_len, dport, reply, sizeof(reply), &reply_len);
        gettimeofday(&t1, NULL);
        if (rc == FL_RESULT_OK) {
            if (out_rtt_ms)
                *out_rtt_ms = timeval_delta_ms(&t0, &t1);
            if (note && note_len > 0)
                snprintf(note, note_len, "loopback tcp rst+ack (in-tree)");
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
