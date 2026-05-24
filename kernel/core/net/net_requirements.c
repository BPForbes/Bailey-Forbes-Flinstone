#include "net_requirements.h"

#include "contract_p0_ci.h"
#include "net_dns.h"
#include "net_ping_host.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fl_result_t fl_net_probe_endpoint(const char *host, uint16_t port, unsigned timeout_ms,
                                  fl_net_requirements_report_t *out) {
    double rtt = 0.0;
    fl_result_t rc;
    char resolved[INET_ADDRSTRLEN];
    uint32_t addr_be = 0;
    const char *skip;

    if (!out || !host || !host[0])
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));

    skip = getenv(FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME);
    if (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE) == 0) {
        snprintf(out->endpoint, sizeof(out->endpoint), "%s:%u", host, (unsigned)port);
        out->ok = 0;
        snprintf(out->detail, sizeof(out->detail), "skipped (SKIP_NETWORK_INTEROP=1)");
        return FL_RESULT_OK;
    }

    if (fl_net_resolve_ipv4(host, &addr_be, resolved, sizeof(resolved)) != FL_RESULT_OK) {
        snprintf(out->endpoint, sizeof(out->endpoint), "%s:%u", host, (unsigned)port);
        snprintf(out->detail, sizeof(out->detail), "dns/resolve failed");
        return FL_RESULT_OK;
    }

    snprintf(out->endpoint, sizeof(out->endpoint), "%s:%u", resolved, (unsigned)port);

    rc = fl_net_ping(host, port, 1u, timeout_ms, &rtt);
    if (rc == FL_RESULT_OK) {
        out->ok = 1;
        out->rtt_ms = rtt;
        snprintf(out->detail, sizeof(out->detail), "%s ok (%.2f ms)",
                 port > 0u ? "tcp" : "icmp", rtt);
    } else {
        out->ok = 0;
        snprintf(out->detail, sizeof(out->detail), "%s failed (%d)",
                 port > 0u ? "tcp" : "icmp", (int)rc);
    }

    return FL_RESULT_OK;
}

void fl_net_print_requirements_report(const fl_net_requirements_report_t *rep) {
    const char *skip;

    if (!rep)
        return;
    printf("  endpoint %s: %s", rep->endpoint, rep->ok ? "ok" : "fail");
    if (rep->ok)
        printf(" (%.2f ms)", rep->rtt_ms);
    printf(" — %s\n", rep->detail);

    skip = getenv(FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME);
    printf("  %s: %s\n", FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_ENV_NAME,
           (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE) == 0)
               ? FL_CONTRACT_P0_CI_SKIP_NETWORK_INTEROP_VALUE
               : "(unset)");
}
