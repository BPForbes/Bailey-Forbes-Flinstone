/*
 * cmd_udp.c — `udpsend` and `udplisten` shell verbs (P3-13 façade over the
 * BSD socket shim). Both verbs route through `fl_net_sock_open(DGRAM)` +
 * bind/connect/send/recv so they exercise the same surface the issue
 * acceptance criteria call out (`fl_socket / fl_bind / fl_send / fl_recv`).
 *
 *   udpsend <ip:port> <message...>
 *       Open a DGRAM socket, connect it to <ip:port> (UDP "connect" just
 *       fixes the default destination), send the joined argv tail, close.
 *
 *   udplisten <port> [-c count] [-W timeout_ms] [-bind <local_ip>]
 *       Bind a DGRAM socket on <port> (default bind 0.0.0.0), recv up to
 *       `count` datagrams (default 1) waiting at most `timeout_ms` per
 *       recv (default 5000 ms), print each payload, close.
 */

#include "cmd_decl.h"
#include "cmd_batch.h"
#include "net_ipv4.h"
#include "net_socket.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UDP_DATAGRAM_MAX 1472u

static int parse_u16(const char *s, uint16_t *out) {
    char *end = NULL;
    long v;
    if (!s || !out)
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
        return -1;
    if (v < 0 || v > 65535)
        return -1;
    *out = (uint16_t)v;
    return 0;
}

static int parse_u32(const char *s, uint32_t *out, uint32_t cap) {
    char *end = NULL;
    long long v;
    if (!s || !out)
        return -1;
    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
        return -1;
    if (v < 0 || (uint64_t)v > cap)
        return -1;
    *out = (uint32_t)v;
    return 0;
}

/* Split "host:port" into addr_be + port_host. Returns 0 on success. */
static int parse_endpoint_v4(const char *s, uint32_t *addr_be_out,
                             uint16_t *port_out) {
    const char *colon;
    char host[64];
    size_t hlen;

    if (!s || !addr_be_out || !port_out)
        return -1;
    colon = strrchr(s, ':');
    if (!colon || colon == s)
        return -1;
    hlen = (size_t)(colon - s);
    if (hlen >= sizeof(host))
        return -1;
    memcpy(host, s, hlen);
    host[hlen] = '\0';
    if (fl_net_ipv4_parse_literal(host, addr_be_out) == 0)
        return -1;
    if (parse_u16(colon + 1, port_out) != 0)
        return -1;
    return 0;
}

int cmd_udpsend_run(int argc, char **argv) {
    uint32_t peer_be = 0u;
    uint16_t peer_port = 0u;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    fl_result_t rc;
    char payload[UDP_DATAGRAM_MAX + 1u];
    size_t off = 0u;
    size_t sent = 0u;
    int i;

    if (argc < 3) {
        fprintf(stderr,
                "udpsend: usage: udpsend <ip:port> <message...>\n");
        return 1;
    }
    if (parse_endpoint_v4(argv[1], &peer_be, &peer_port) != 0) {
        fprintf(stderr, "udpsend: invalid endpoint '%s'\n", argv[1]);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        size_t n = strlen(argv[i]);
        if (off + n + 1u >= sizeof(payload)) {
            fprintf(stderr, "udpsend: message exceeds %u-byte datagram cap\n",
                    UDP_DATAGRAM_MAX);
            return 1;
        }
        if (off > 0u)
            payload[off++] = ' ';
        memcpy(&payload[off], argv[i], n);
        off += n;
    }
    payload[off] = '\0';

    (void)fl_net_sock_init();
    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_DGRAM, &h);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "udpsend: socket open failed (rc=%d)\n", (int)rc);
        return 1;
    }
    rc = fl_net_sock_connect(h, peer_be, peer_port);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "udpsend: connect failed (rc=%d)\n", (int)rc);
        fl_net_sock_close(h);
        return 1;
    }
    rc = fl_net_sock_send(h, payload, off, &sent);
    if (rc != FL_RESULT_OK || sent != off) {
        fprintf(stderr,
                "udpsend: send failed (rc=%d, sent=%zu of %zu)\n",
                (int)rc, sent, off);
        fl_net_sock_close(h);
        return 1;
    }
    fl_net_sock_close(h);
    {
        char ip[32];
        fl_net_ipv4_format_addr(peer_be, ip, sizeof(ip));
        printf("udpsend: %zu bytes -> %s:%u\n", sent, ip, (unsigned)peer_port);
    }
    return 0;
}

int cmd_udplisten_run(int argc, char **argv) {
    uint16_t bind_port = 0u;
    uint32_t bind_addr_be = 0u; /* 0.0.0.0 */
    uint32_t count = 1u;
    uint32_t timeout_ms = 5000u;
    fl_net_sock_handle_t h = FL_NET_SOCK_INVALID;
    fl_result_t rc;
    uint8_t buf[UDP_DATAGRAM_MAX + 1u];
    uint32_t got = 0u;
    int i;

    if (argc < 2) {
        fprintf(stderr,
                "udplisten: usage: udplisten <port> "
                "[-c count] [-W timeout_ms] [-bind <local_ip>]\n");
        return 1;
    }
    if (parse_u16(argv[1], &bind_port) != 0 || bind_port == 0u) {
        fprintf(stderr, "udplisten: invalid port '%s'\n", argv[1]);
        return 1;
    }
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (parse_u32(argv[i + 1], &count, 1000000u) != 0 || count == 0u) {
                fprintf(stderr, "udplisten: bad -c value\n");
                return 1;
            }
            i++;
            continue;
        }
        if (!strcmp(argv[i], "-W") && i + 1 < argc) {
            if (parse_u32(argv[i + 1], &timeout_ms, 600000u) != 0) {
                fprintf(stderr, "udplisten: bad -W value\n");
                return 1;
            }
            i++;
            continue;
        }
        if (!strcmp(argv[i], "-bind") && i + 1 < argc) {
            if (fl_net_ipv4_parse_literal(argv[i + 1], &bind_addr_be) == 0) {
                fprintf(stderr, "udplisten: bad -bind IPv4 '%s'\n",
                        argv[i + 1]);
                return 1;
            }
            i++;
            continue;
        }
        fprintf(stderr, "udplisten: unknown option '%s'\n", argv[i]);
        return 1;
    }

    (void)fl_net_sock_init();
    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_DGRAM, &h);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "udplisten: socket open failed (rc=%d)\n", (int)rc);
        return 1;
    }
    rc = fl_net_sock_bind(h, bind_addr_be, bind_port);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "udplisten: bind on port %u failed (rc=%d)\n",
                (unsigned)bind_port, (int)rc);
        fl_net_sock_close(h);
        return 1;
    }
    {
        char ip[32];
        fl_net_ipv4_format_addr(bind_addr_be, ip, sizeof(ip));
        printf("udplisten: listening on %s:%u, count=%u, timeout=%ums\n",
               ip, (unsigned)bind_port, (unsigned)count, (unsigned)timeout_ms);
    }
    for (uint32_t k = 0u; k < count; k++) {
        size_t n = 0u;
        rc = fl_net_sock_recv(h, buf, sizeof(buf) - 1u, &n, timeout_ms);
        if (rc == FL_RESULT_TIMEDOUT) {
            fprintf(stderr,
                    "udplisten: timeout after %u datagram%s\n",
                    (unsigned)got, got == 1u ? "" : "s");
            fl_net_sock_close(h);
            return got == 0u ? 1 : 0;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "udplisten: recv failed (rc=%d)\n", (int)rc);
            fl_net_sock_close(h);
            return 1;
        }
        buf[n] = '\0';
        printf("udplisten[%u]: %zu bytes: %s\n", (unsigned)k, n, (char *)buf);
        got++;
    }
    fl_net_sock_close(h);
    return 0;
}

__attribute__((used))
int cmd_udpsend_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    /* udpsend <ip:port> <message...>. The runtime concatenates every
     * remaining argv token as part of the message. In batch mode that
     * would swallow the next builtin (e.g. `udpsend X:1 hi help` should
     * still run `help`), so stop on the first token that is a known
     * shell command and on the first '-' flag (the runtime rejects
     * those with an error). */
    while (j < argc) {
        if (argv[j][0] == '-')
            break;
        if (fl_shell_cmd_lookup(argv[j]) != FL_SCMD_UNKNOWN)
            break;
        used++;
        j++;
    }
    return used;
}

__attribute__((used))
int cmd_udplisten_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;
    (void)argc;
    while (j < argc) {
        if (!strcmp(argv[j], "-c") || !strcmp(argv[j], "-W") ||
            !strcmp(argv[j], "-bind")) {
            if (j + 1 >= argc)
                return used + 1;
            used += 2;
            j += 2;
            continue;
        }
        if (argv[j][0] == '-')
            break;
        /* Only the leading <port> positional is allowed. */
        if (used >= 2)
            break;
        used++;
        j++;
    }
    return used;
}
