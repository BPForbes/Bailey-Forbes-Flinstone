#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "net_dns.h"
#include "net_endpoint.h"
#include "net_ipv6.h"
#include "net_ping6_host.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int host_needs_netdev_io6(const char *host) {
    uint8_t dst6[FL_NET_IPV6_ADDR_LEN];
    fl_net_endpoint_t ep;

    if (!host || !host[0])
        return 0;
    if (strcmp(host, "localhost") == 0)
        return 0;
    if (fl_net_dns_resolve_aaaa(host, dst6, NULL, 0) == FL_RESULT_OK)
        return !fl_net_ipv6_is_loopback(dst6);
    if (fl_net_endpoint_parse(host, &ep) && ep.family == FL_NET_ADDR_FAMILY_V6)
        return !fl_net_ipv6_is_loopback(ep.addr.v6_be);
    return 1;
}

int cmd_ping6_run(int argc, char **argv) {
    const char *host = NULL;
    unsigned count = 1u;
    unsigned timeout_ms = 2000u;
    double rtt = 0.0;
    fl_result_t rc;
    char msg[256];
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "-W")) {
            char *end = NULL;
            unsigned long v;
            const char *opt = argv[i];

            if (i + 1 >= argc) {
                fprintf(stderr, "ping6: missing value for %s\n", opt);
                return 1;
            }
            errno = 0;
            v = strtoul(argv[i + 1], &end, 10);
            if (errno != 0 || end == argv[i + 1] || (end && *end != '\0')) {
                fprintf(stderr, "ping6: invalid value for %s\n", opt);
                return 1;
            }
            i++;
            if (!strcmp(opt, "-c")) {
                if (v == 0 || v > 16) {
                    fprintf(stderr, "ping6: -c must be 1..16\n");
                    return 1;
                }
                count = (unsigned)v;
            } else {
                if (v < 100 || v > 30000) {
                    fprintf(stderr, "ping6: -W must be 100..30000\n");
                    return 1;
                }
                timeout_ms = (unsigned)v;
            }
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "ping6: unknown option '%s'\n", argv[i]);
            return 1;
        }
        if (!host) {
            host = argv[i];
            continue;
        }
        fprintf(stderr, "ping6: unexpected argument '%s'\n", argv[i]);
        return 1;
    }

    if (!host) {
        fprintf(stderr, "ping6: usage: ping6 [-c count] [-W ms] <host>\n");
        return 1;
    }

    if (host_needs_netdev_io6(host)) {
        if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
            FL_AUTHZ_DENY) {
            fl_net_ping6_format_result(host, FL_RESULT_ACCES, 0.0, msg, sizeof(msg));
            puts(msg);
            return 1;
        }
    }

    rc = fl_net_ping6(host, count, timeout_ms, &rtt);
    fl_net_ping6_format_result(host, rc, rtt, msg, sizeof(msg));
    puts(msg);
    return (rc == FL_RESULT_OK) ? 0 : 1;
}

__attribute__((used))
int cmd_ping6_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    int j = i + 1;

    (void)argc;
    while (j < argc && argv[j][0] == '-') {
        if (!strcmp(argv[j], "-c") || !strcmp(argv[j], "-W")) {
            if (j + 1 >= argc)
                return used + 1;
            return used + 3;
        }
        return used + 1;
    }
    if (j < argc)
        return used + 2;
    return used;
}
