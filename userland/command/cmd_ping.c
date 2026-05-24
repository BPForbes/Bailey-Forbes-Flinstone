#include "cmd_decl.h"
#include "cmd_batch.h"
#include "fl/authz_subsystem.h"
#include "contract_p2_authz.h"
#include "net_ping_host.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

static int host_needs_netdev_io(const char *host) {
    struct in_addr addr;

    if (!host || !host[0])
        return 0;
    if (strcmp(host, "localhost") == 0)
        return 0;
    if (inet_pton(AF_INET, host, &addr) != 1)
        return 1;
    return !fl_net_ipv4_is_loopback(addr.s_addr);
}

static void print_ping_result(const char *host, fl_result_t rc, double rtt_ms) {
    switch (rc) {
    case FL_RESULT_OK:
        printf("ping %s: ok (%.2f ms)\n", host, rtt_ms);
        break;
    case FL_RESULT_TIMEDOUT:
        printf("ping %s: timed out\n", host);
        break;
    case FL_RESULT_NOSYS:
        printf("ping %s: ICMP not available on this host (need Linux ping socket)\n", host);
        break;
    case FL_RESULT_INVAL:
        printf("ping: invalid host %s\n", host);
        break;
    case FL_RESULT_ACCES:
        printf("ping %s: permission denied (netdev I/O)\n", host);
        break;
    default:
        printf("ping %s: failed (%d)\n", host, (int)rc);
        break;
    }
}

int cmd_ping_run(int argc, char **argv) {
    const char *host = "127.0.0.1";
    unsigned count = 1u;
    unsigned timeout_ms = 2000u;
    double rtt = 0.0;
    fl_result_t rc;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            count = (unsigned)atoi(argv[++i]);
            continue;
        }
        if (!strcmp(argv[i], "-W") && i + 1 < argc) {
            timeout_ms = (unsigned)atoi(argv[++i]);
            continue;
        }
        if (argv[i][0] == '-')
            continue;
        host = argv[i];
    }

    if (host_needs_netdev_io(host)) {
        if (fl_authz_subsystem_check((unsigned)FL_AUTHZ_OP_NETDEV_IO, NULL) ==
            FL_AUTHZ_DENY) {
            print_ping_result(host, FL_RESULT_ACCES, 0.0);
            return 1;
        }
    }

    rc = fl_net_ping_ipv4(host, count, timeout_ms, &rtt);
    print_ping_result(host, rc, rtt);
    return (rc == FL_RESULT_OK) ? 0 : 1;
}

int cmd_ping_batch_tokens_count(int argc, char **argv, int i) {
    int used = 1;
    if (i + 1 < argc && argv[i + 1][0] != '-')
        used++;
    if (i + 2 < argc && !strcmp(argv[i], "-c"))
        used += 2;
    if (i + 2 < argc && !strcmp(argv[i], "-W"))
        used += 2;
    (void)argc;
    return used;
}
