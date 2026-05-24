#include "net_ping_host.h"

#include "net_dns.h"
#include "net_icmp.h"
#include "net_ipv4.h"
#include "net_tcp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char s_last_tcp_note[96];

fl_result_t fl_net_ping(const char *host, uint16_t port, unsigned count,
                        unsigned timeout_ms, double *out_rtt_ms) {
    uint32_t addr_be = 0;
    char resolved[32];
    uint16_t id = (uint16_t)(getpid() & 0xffff);
    unsigned i;

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
        return fl_net_tcp_syn_probe(addr_be, port, timeout_ms, out_rtt_ms, s_last_tcp_note,
                                    sizeof(s_last_tcp_note));

    for (i = 0; i < count; i++) {
        fl_result_t rc =
            fl_net_icmp_echo_exchange(addr_be, id, (uint16_t)(i + 1u), timeout_ms, out_rtt_ms);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_TIMEDOUT)
            return rc;
    }
    return FL_RESULT_TIMEDOUT;
}

void fl_net_ping_format_result(const char *host, uint16_t port, fl_result_t rc,
                               double rtt_ms, char *buf, size_t buf_len) {
    char resolved[32];
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
        snprintf(buf, buf_len, "ping %s: permission denied (raw/tcp or netdev)", host);
        break;
    default:
        snprintf(buf, buf_len, "ping %s (%s:%u) %s: failed (%d)", host, resolved,
                 port > 0u ? (unsigned)port : 0u, proto, (int)rc);
        break;
    }
}
