#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "net_dns.h"
#include "net_ipv4.h"
#include "net_ping_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int host_needs_netdev_io(const char *host) {
    uint32_t addr_be = 0;

    if (!host || !host[0])
        return 0;
    if (strcmp(host, "localhost") == 0)
        return 0;
    if (fl_net_ipv4_parse_literal(host, &addr_be))
        return !fl_net_ipv4_is_loopback(addr_be);
    return 1; /* hostnames may require DNS/network I/O */
}

static int parse_port_arg(const char *s, uint16_t *out) {
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

int cmd_ping_run(int argc, char **argv) {
    const char *host = NULL;
    uint16_t port = 0;
    unsigned count = 1u;
    unsigned timeout_ms = 2000u;
    double rtt = 0.0;
    fl_result_t rc;
    char msg[256];
    char tcp_note[96] = {0};
    int i;
    int host_set = 0;
    int port_set = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "-W")) {
            uint16_t parsed = 0;
            const char *opt = argv[i];

            if (i + 1 >= argc || parse_port_arg(argv[i + 1], &parsed) != 0) {
                fprintf(stderr, "ping: invalid value for %s\n", opt);
                return 1;
            }
            i++;
            if (!strcmp(opt, "-c")) {
                if (parsed == 0) {
                    fprintf(stderr, "ping: -c must be >= 1\n");
                    return 1;
                }
                count = (unsigned)parsed;
            } else {
                timeout_ms = (unsigned)parsed;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "ping: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (!host_set) {
            host = argv[i];
            host_set = 1;
            continue;
        }
        if (!port_set && parse_port_arg(argv[i], &port) == 0) {
            port_set = 1;
            continue;
        }
        fprintf(stderr, "ping: unexpected argument '%s'\n", argv[i]);
        return 1;
    }

    if (!host) {
        fprintf(stderr, "ping: usage: ping <host> [port] [-c count] [-W timeout_ms]\n");
        fprintf(stderr, "       port 0 or omitted = ICMP echo; port 1-65535 = TCP connect\n");
        return 1;
    }

    if (host_needs_netdev_io(host)) {
        if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
            FL_AUTHZ_DENY) {
            fl_net_ping_format_result(host, port, FL_RESULT_ACCES, 0.0, msg, sizeof(msg), NULL);
            fputs(msg, stdout);
            fputc('\n', stdout);
            return 1;
        }
    }

    rc = fl_net_ping(host, port, count, timeout_ms, &rtt, tcp_note, sizeof(tcp_note));
    fl_net_ping_format_result(host, port, rc, rtt, msg, sizeof(msg), tcp_note);
    fputs(msg, stdout);
    fputc('\n', stdout);
    return (rc == FL_RESULT_OK) ? 0 : 1;
}

__attribute__((used))
int cmd_ping_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int pos = 0;
    int j = i + 1;

    (void)argc;
    while (j < argc) {
        if (!strcmp(argv[j], "-c") || !strcmp(argv[j], "-W")) {
            if (j + 1 >= argc)
                return used + 1;
            used += 2;
            j += 2;
            continue;
        }
        if (argv[j][0] == '-')
            break;
        if (pos >= 2)
            break;
        used++;
        pos++;
        j++;
    }
    return used;
}
