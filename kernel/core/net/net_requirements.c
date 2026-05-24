#include "net_requirements.h"

#include "contract_p0_ci.h"
#include "net_ping_host.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static int tcp_connect_probe(const char *host, uint16_t port, unsigned timeout_ms) {
    struct sockaddr_in addr;
    int sock;
    int flags;
    int rc;
    fd_set wfds;
    struct timeval tv;
    int so_error = 0;
    socklen_t slen = sizeof(so_error);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1)
        return 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return 0;

    flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
        (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (rc == 0) {
        close(sock);
        return 1;
    }
    if (errno != EINPROGRESS) {
        close(sock);
        return 0;
    }

    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);
    rc = select(sock + 1, NULL, &wfds, NULL, &tv);
    if (rc <= 0) {
        close(sock);
        return 0;
    }
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &slen) != 0 || so_error != 0) {
        close(sock);
        return 0;
    }
    close(sock);
    return 1;
}

fl_result_t fl_net_probe_requirements(int probe_internet,
                                      fl_net_requirements_report_t *out) {
    double rtt = 0.0;
    fl_result_t rc;

    if (!out)
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));
    out->internet_detail = "not probed";

    rc = fl_net_ping_ipv4("127.0.0.1", 1u, 2000u, &rtt);
    if (rc == FL_RESULT_OK) {
        out->loopback_icmp_ok = 1;
        out->loopback_rtt_ms = rtt;
    } else {
        out->loopback_icmp_ok = 0;
        out->loopback_rtt_ms = 0.0;
    }

    if (!probe_internet) {
        out->internet_skipped = 1;
        out->internet_detail = "skipped (caller)";
        return FL_RESULT_OK;
    }

    const char *skip = getenv(FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME);
    if (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE) == 0) {
        out->internet_skipped = 1;
        out->internet_detail = "skipped (SKIP_NETWORK_INTEROP=1)";
        return FL_RESULT_OK;
    }

    out->internet_skipped = 0;
    if (tcp_connect_probe("1.1.1.1", 443, 4000u)) {
        out->internet_tcp_ok = 1;
        out->internet_detail = "tcp 1.1.1.1:443 ok";
    } else if (tcp_connect_probe("8.8.8.8", 53, 4000u)) {
        out->internet_tcp_ok = 1;
        out->internet_detail = "tcp 8.8.8.8:53 ok";
    } else {
        out->internet_tcp_ok = 0;
        out->internet_detail = "no route to public resolver";
    }

    return FL_RESULT_OK;
}

void fl_net_print_requirements_report(const fl_net_requirements_report_t *rep) {
    if (!rep)
        return;
    printf("  loopback_icmp (127.0.0.1): %s",
           rep->loopback_icmp_ok ? "ok" : "fail");
    if (rep->loopback_icmp_ok)
        printf(" (%.2f ms)", rep->loopback_rtt_ms);
    putchar('\n');
    if (rep->internet_skipped) {
        printf("  internet_tcp: skip (%s)\n", rep->internet_detail);
    } else {
        printf("  internet_tcp: %s (%s)\n",
               rep->internet_tcp_ok ? "ok" : "fail", rep->internet_detail);
    }
    {
        const char *skip = getenv(FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME);
        printf("  %s: %s\n", FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME,
               (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE) == 0)
                   ? FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE
                   : "(unset)");
    }
}
