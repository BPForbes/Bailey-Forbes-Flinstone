#include "net_ping_host.h"

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#if defined(__linux__)
#include <netinet/ip_icmp.h>
#endif

int fl_net_ipv4_is_loopback(uint32_t addr_be) {
    uint32_t host = ntohl(addr_be);
    return (host >> 24) == 127u;
}

static uint16_t icmp_checksum(const void *data, size_t len) {
    const uint16_t *w = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *w++;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)(*(const uint8_t *)w) << 8;
    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum);
}

#if defined(__linux__)

static fl_result_t ping_via_tcp_connect(const char *host, unsigned timeout_ms,
                                        double *out_rtt_ms) {
    struct sockaddr_in addr;
    struct timeval tv_start, tv_end;
    int sock;
    int flags;
    int so_error = 0;
    socklen_t slen = sizeof(so_error);
    fd_set wfds;
    struct timeval tv;
    int rc;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9); /* discard — RST still proves local stack */
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
        return FL_RESULT_INVAL;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return FL_RESULT_ERR;

    flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    gettimeofday(&tv_start, NULL);
    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        gettimeofday(&tv_end, NULL);
        close(sock);
        if (out_rtt_ms) {
            *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
            *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
        }
        return FL_RESULT_OK;
    }
    if (errno != EINPROGRESS) {
        if (errno == ECONNREFUSED || errno == EHOSTUNREACH) {
            gettimeofday(&tv_end, NULL);
            close(sock);
            if (out_rtt_ms) {
                *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
                *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
            }
            return FL_RESULT_OK;
        }
        close(sock);
        return FL_RESULT_ERR;
    }

    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    rc = select(sock + 1, NULL, &wfds, NULL, &tv);
    gettimeofday(&tv_end, NULL);
    if (rc <= 0) {
        close(sock);
        return FL_RESULT_TIMEDOUT;
    }
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &slen) != 0) {
        close(sock);
        return FL_RESULT_ERR;
    }
    close(sock);
    if (so_error != 0 && so_error != ECONNREFUSED)
        return FL_RESULT_ERR;
    if (out_rtt_ms) {
        *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
        *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    }
    return FL_RESULT_OK;
}

static fl_result_t ping_via_subprocess(const char *host, unsigned count,
                                         unsigned timeout_ms, double *out_rtt_ms) {
    char cmd[320];
    FILE *fp;
    char line[256];
    int seen_ok = 0;
    double last_ms = 0.0;
    unsigned secs = (timeout_ms + 999u) / 1000u;
    if (secs < 1u)
        secs = 1u;

    snprintf(cmd, sizeof(cmd), "ping -c %u -W %u %s 2>/dev/null", count, secs, host);
    fp = popen(cmd, "r");
    if (!fp)
        return FL_RESULT_ERR;
    while (fgets(line, sizeof(line), fp)) {
        char *t = strstr(line, "time=");
        if (t) {
            seen_ok = 1;
            last_ms = strtod(t + 5, NULL);
        }
        if (strstr(line, " 0% packet loss") || strstr(line, " 0.0% packet loss"))
            seen_ok = 1;
    }
    if (pclose(fp) != 0 && !seen_ok) {
        fl_result_t tcp = ping_via_tcp_connect(host, timeout_ms, out_rtt_ms);
        return tcp;
    }
    if (!seen_ok) {
        fl_result_t tcp = ping_via_tcp_connect(host, timeout_ms, out_rtt_ms);
        return tcp;
    }
    if (out_rtt_ms)
        *out_rtt_ms = last_ms;
    return FL_RESULT_OK;
}

static fl_result_t ping_once_linux(int sock, struct sockaddr_in *dst, uint16_t seq,
                                   unsigned timeout_ms, double *out_rtt_ms) {
    uint8_t pkt[64];
    struct icmphdr *icmp = (struct icmphdr *)pkt;
    struct timeval tv_start, tv_end, tv_to;
    ssize_t n;
    uint16_t my_id = (uint16_t)(getpid() & 0xffff);

    memset(pkt, 0, sizeof(pkt));
    icmp->type = ICMP_ECHO;
    icmp->code = 0;
    icmp->un.echo.id = htons(my_id);
    icmp->un.echo.sequence = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = icmp_checksum(pkt, 8);

    gettimeofday(&tv_start, NULL);
    if (sendto(sock, pkt, 8, 0, (struct sockaddr *)dst, sizeof(*dst)) < 0) {
        if (errno == EACCES)
            return FL_RESULT_ACCES;
        return FL_RESULT_ERR;
    }

    tv_to.tv_sec = (time_t)(timeout_ms / 1000u);
    tv_to.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv_to, sizeof(tv_to)) != 0)
        return FL_RESULT_ERR;

    for (;;) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        uint8_t rx[128];

        n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from, &fromlen);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return FL_RESULT_TIMEDOUT;
            return FL_RESULT_ERR;
        }
        if (n < (ssize_t)sizeof(struct icmphdr))
            continue;
        {
            const struct icmphdr *rx_icmp = (const struct icmphdr *)rx;
            if (rx_icmp->type == ICMP_ECHOREPLY &&
                ntohs(rx_icmp->un.echo.id) == my_id &&
                ntohs(rx_icmp->un.echo.sequence) == seq) {
                gettimeofday(&tv_end, NULL);
                if (out_rtt_ms) {
                    double ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
                    ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
                    *out_rtt_ms = ms;
                }
                return FL_RESULT_OK;
            }
        }
    }
}

fl_result_t fl_net_ping_ipv4(const char *host, unsigned count, unsigned timeout_ms,
                             double *out_rtt_ms) {
    struct sockaddr_in dst;
    int sock;
    fl_result_t last = FL_RESULT_TIMEDOUT;
    unsigned i;

    if (!host || !host[0])
        return FL_RESULT_INVAL;
    if (count < 1u)
        count = 1u;
    if (count > 16u)
        count = 16u;
    if (timeout_ms < 100u)
        timeout_ms = 100u;
    if (timeout_ms > 30000u)
        timeout_ms = 30000u;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, host, &dst.sin_addr) != 1)
        return FL_RESULT_INVAL;

    /* Unprivileged hosted shells (CI, cloud agents): system ping, then TCP reachability. */
    if (geteuid() != 0) {
        fl_result_t rc = ping_via_subprocess(host, count, timeout_ms, out_rtt_ms);
        if (rc == FL_RESULT_OK)
            return rc;
        return ping_via_tcp_connect(host, timeout_ms, out_rtt_ms);
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (sock < 0) {
        if (errno == EACCES)
            return ping_via_subprocess(host, count, timeout_ms, out_rtt_ms);
        return (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) ? FL_RESULT_NOSYS
                                                                  : FL_RESULT_ERR;
    }

    for (i = 0; i < count; i++) {
        double rtt = 0.0;
        fl_result_t rc = ping_once_linux(sock, &dst, (uint16_t)(i + 1u), timeout_ms, &rtt);
        last = rc;
        if (rc == FL_RESULT_OK && out_rtt_ms)
            *out_rtt_ms = rtt;
        if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
            break;
    }

    close(sock);
    if (last != FL_RESULT_OK && (last == FL_RESULT_ACCES || last == FL_RESULT_ERR))
        return ping_via_subprocess(host, count, timeout_ms, out_rtt_ms);
    return last;
}

#else /* !__linux__ */

fl_result_t fl_net_ping_ipv4(const char *host, unsigned count, unsigned timeout_ms,
                             double *out_rtt_ms) {
    (void)host;
    (void)count;
    (void)timeout_ms;
    (void)out_rtt_ms;
    return FL_RESULT_NOSYS;
}

#endif
