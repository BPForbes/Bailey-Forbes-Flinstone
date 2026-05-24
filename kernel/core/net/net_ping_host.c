#include "net_ping_host.h"

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

fl_result_t fl_net_resolve_ipv4(const char *host, uint32_t *out_addr_be,
                                char *resolved_ip, size_t resolved_ip_len) {
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *rp;
    const struct sockaddr_in *sin;
    int gai;

    if (!host || !host[0] || !out_addr_be || !resolved_ip || resolved_ip_len < 16u)
        return FL_RESULT_INVAL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    gai = getaddrinfo(host, NULL, &hints, &res);
    if (gai != 0)
        return FL_RESULT_INVAL;

    for (rp = res; rp; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET && rp->ai_addrlen >= sizeof(struct sockaddr_in))
            break;
    }
    if (!rp) {
        freeaddrinfo(res);
        return FL_RESULT_INVAL;
    }

    sin = (const struct sockaddr_in *)rp->ai_addr;
    *out_addr_be = sin->sin_addr.s_addr;
    if (!inet_ntop(AF_INET, &sin->sin_addr, resolved_ip, resolved_ip_len)) {
        freeaddrinfo(res);
        return FL_RESULT_ERR;
    }
    freeaddrinfo(res);
    return FL_RESULT_OK;
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

static fl_result_t ping_once_icmp_linux(int sock, const struct sockaddr_in *dst,
                                          uint16_t seq, unsigned timeout_ms,
                                          double *out_rtt_ms) {
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
    if (sendto(sock, pkt, 8, 0, (const struct sockaddr *)dst, sizeof(*dst)) < 0) {
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

static fl_result_t ping_icmp_subprocess(const char *host, unsigned count,
                                        unsigned timeout_ms, double *out_rtt_ms) {
    char cmd[384];
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
    if (pclose(fp) != 0 && !seen_ok)
        return FL_RESULT_TIMEDOUT;
    if (!seen_ok)
        return FL_RESULT_TIMEDOUT;
    if (out_rtt_ms)
        *out_rtt_ms = last_ms;
    return FL_RESULT_OK;
}

static fl_result_t ping_icmp_linux(const char *host, uint32_t addr_be, unsigned count,
                                   unsigned timeout_ms, double *out_rtt_ms) {
    struct sockaddr_in dst;
    int sock;
    fl_result_t last = FL_RESULT_TIMEDOUT;
    unsigned i;

    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = addr_be;

    if (geteuid() != 0)
        return ping_icmp_subprocess(host, count, timeout_ms, out_rtt_ms);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (sock < 0) {
        if (errno == EACCES)
            return ping_icmp_subprocess(host, count, timeout_ms, out_rtt_ms);
        return (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) ? FL_RESULT_NOSYS
                                                                  : FL_RESULT_ERR;
    }

    for (i = 0; i < count; i++) {
        double rtt = 0.0;
        fl_result_t rc =
            ping_once_icmp_linux(sock, &dst, (uint16_t)(i + 1u), timeout_ms, &rtt);
        last = rc;
        if (rc == FL_RESULT_OK && out_rtt_ms)
            *out_rtt_ms = rtt;
        if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
            break;
    }

    close(sock);
    if (last != FL_RESULT_OK &&
        (last == FL_RESULT_ACCES || last == FL_RESULT_ERR))
        return ping_icmp_subprocess(host, count, timeout_ms, out_rtt_ms);
    return last;
}

/** TCP connect to **addr_be**:**port**; records RTT and optional **tcp_note** (caller buffer). */
static fl_result_t ping_tcp_linux(uint32_t addr_be, uint16_t port, unsigned timeout_ms,
                                  double *out_rtt_ms, char *tcp_note, size_t tcp_note_len) {
    struct sockaddr_in addr;
    struct timeval tv_start, tv_end;
    int sock;
    int flags;
    int so_error = 0;
    socklen_t slen = sizeof(so_error);
    fd_set wfds;
    struct timeval tv;
    int rc;

    if (port == 0u)
        return FL_RESULT_INVAL;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = addr_be;
    addr.sin_port = htons(port);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return FL_RESULT_ERR;

    flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    gettimeofday(&tv_start, NULL);
    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    gettimeofday(&tv_end, NULL);

    if (rc == 0) {
        close(sock);
        if (out_rtt_ms) {
            *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
            *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
        }
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp connected");
        return FL_RESULT_OK;
    }
    if (errno == ECONNREFUSED) {
        close(sock);
        if (out_rtt_ms) {
            *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
            *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
        }
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp refused (host reachable)");
        return FL_RESULT_OK;
    }
    if (errno != EINPROGRESS) {
        close(sock);
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp error (%s)", strerror(errno));
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
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp timeout");
        return FL_RESULT_TIMEDOUT;
    }
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &slen) != 0) {
        close(sock);
        return FL_RESULT_ERR;
    }
    close(sock);
    if (out_rtt_ms) {
        *out_rtt_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0;
        *out_rtt_ms += (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    }
    if (so_error == 0) {
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp connected");
        return FL_RESULT_OK;
    }
    if (so_error == ECONNREFUSED) {
        if (tcp_note && tcp_note_len > 0)
            snprintf(tcp_note, tcp_note_len, "tcp refused (host reachable)");
        return FL_RESULT_OK;
    }
    if (tcp_note && tcp_note_len > 0)
        snprintf(tcp_note, tcp_note_len, "tcp error (%s)", strerror(so_error));
    return FL_RESULT_ERR;
}

static char s_last_tcp_note[64];

fl_result_t fl_net_ping(const char *host, uint16_t port, unsigned count,
                        unsigned timeout_ms, double *out_rtt_ms) {
    uint32_t addr_be = 0;
    char resolved[INET_ADDRSTRLEN];

    s_last_tcp_note[0] = '\0';

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

    if (fl_net_resolve_ipv4(host, &addr_be, resolved, sizeof(resolved)) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    if (port > 0u)
        return ping_tcp_linux(addr_be, port, timeout_ms, out_rtt_ms, s_last_tcp_note,
                            sizeof(s_last_tcp_note));

    return ping_icmp_linux(host, addr_be, count, timeout_ms, out_rtt_ms);
}

void fl_net_ping_format_result(const char *host, uint16_t port, fl_result_t rc,
                               double rtt_ms, char *buf, size_t buf_len) {
    char resolved[INET_ADDRSTRLEN];
    uint32_t addr_be = 0;
    const char *proto;
    const char *extra = "";

    if (!buf || buf_len == 0)
        return;
    buf[0] = '\0';

    if (fl_net_resolve_ipv4(host, &addr_be, resolved, sizeof(resolved)) != FL_RESULT_OK)
        snprintf(resolved, sizeof(resolved), "%s", host ? host : "?");

    proto = (port > 0u) ? "tcp" : "icmp";
    if (port > 0u && s_last_tcp_note[0])
        extra = s_last_tcp_note;

    switch (rc) {
    case FL_RESULT_OK:
        if (port > 0u && extra[0])
            snprintf(buf, buf_len, "ping %s (%s:%u) %s: ok (%.2f ms) [%s]", host, resolved,
                     (unsigned)port, proto, rtt_ms, extra);
        else
            snprintf(buf, buf_len, "ping %s (%s) %s: ok (%.2f ms)", host, resolved, proto,
                     rtt_ms);
        break;
    case FL_RESULT_TIMEDOUT:
        snprintf(buf, buf_len, "ping %s (%s:%u) %s: timed out", host, resolved,
                 port > 0u ? (unsigned)port : 0u, proto);
        break;
    case FL_RESULT_NOSYS:
        snprintf(buf, buf_len, "ping %s: %s not available on this host", host, proto);
        break;
    case FL_RESULT_INVAL:
        snprintf(buf, buf_len, "ping: cannot resolve or invalid host %s", host ? host : "");
        break;
    case FL_RESULT_ACCES:
        snprintf(buf, buf_len, "ping %s: permission denied (netdev I/O)", host);
        break;
    default:
        snprintf(buf, buf_len, "ping %s (%s:%u) %s: failed (%d)", host, resolved,
                 port > 0u ? (unsigned)port : 0u, proto, (int)rc);
        break;
    }
}

#else /* !__linux__ */

fl_result_t fl_net_ping(const char *host, uint16_t port, unsigned count,
                        unsigned timeout_ms, double *out_rtt_ms) {
    (void)host;
    (void)port;
    (void)count;
    (void)timeout_ms;
    (void)out_rtt_ms;
    return FL_RESULT_NOSYS;
}

void fl_net_ping_format_result(const char *host, uint16_t port, fl_result_t rc,
                               double rtt_ms, char *buf, size_t buf_len) {
    if (buf && buf_len > 0)
        snprintf(buf, buf_len, "ping %s: not supported on this platform (%d)", host ? host : "",
                 (int)rc);
    (void)port;
    (void)rtt_ms;
}

#endif
