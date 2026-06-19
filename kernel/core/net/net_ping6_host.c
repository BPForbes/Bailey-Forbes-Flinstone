#include "net_ping6_host.h"

#include "net_dns.h"
#include "net_endpoint.h"
#include "net_icmpv6.h"
#include "net_ipv6.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static fl_result_t resolve_dst6(const char *host, uint8_t dst6[FL_NET_IPV6_ADDR_LEN],
                                char *resolved_txt, size_t resolved_cap) {
    fl_net_endpoint_t ep;
    char aaaa[64];

    if (!host || !host[0] || !dst6)
        return FL_RESULT_INVAL;

    if (host[0] == '[') {
        if (!fl_net_endpoint_parse(host, &ep) || ep.family != FL_NET_ADDR_FAMILY_V6)
            return FL_RESULT_INVAL;
        memcpy(dst6, ep.addr.v6_be, FL_NET_IPV6_ADDR_LEN);
        if (resolved_txt && resolved_cap > 0u) {
            if (!fl_net_ipv6_format_addr(dst6, aaaa, sizeof(aaaa)))
                return FL_RESULT_ERR;
            snprintf(resolved_txt, resolved_cap, "%s", aaaa);
        }
        return FL_RESULT_OK;
    }

    if (fl_net_dns_resolve_aaaa(host, dst6, aaaa, sizeof(aaaa)) == FL_RESULT_OK) {
        if (resolved_txt && resolved_cap > 0u)
            snprintf(resolved_txt, resolved_cap, "%s", aaaa);
        return FL_RESULT_OK;
    }

    if (fl_net_endpoint_parse(host, &ep) && ep.family == FL_NET_ADDR_FAMILY_V6) {
        memcpy(dst6, ep.addr.v6_be, FL_NET_IPV6_ADDR_LEN);
        if (resolved_txt && resolved_cap > 0u &&
            !fl_net_ipv6_format_addr(dst6, resolved_txt, resolved_cap))
            return FL_RESULT_ERR;
        return FL_RESULT_OK;
    }

    return FL_RESULT_INVAL;
}

fl_result_t fl_net_ping6(const char *host, unsigned count, unsigned timeout_ms,
                         double *out_rtt_ms) {
    uint8_t dst6[FL_NET_IPV6_ADDR_LEN];
    uint16_t id = (uint16_t)(getpid() & 0xffff);
    unsigned i;

    if (count < 1u)
        count = 1u;
    if (count > 16u)
        count = 16u;
    if (timeout_ms < 100u)
        timeout_ms = 100u;
    if (timeout_ms > 30000u)
        timeout_ms = 30000u;

    if (resolve_dst6(host, dst6, NULL, 0) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    for (i = 0; i < count; i++) {
        fl_result_t rc =
            fl_net_icmpv6_echo_exchange(dst6, id, (uint16_t)(i + 1u), timeout_ms, out_rtt_ms);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_TIMEDOUT)
            return rc;
    }
    return FL_RESULT_TIMEDOUT;
}

void fl_net_ping6_format_result(const char *host, fl_result_t rc, double rtt_ms, char *buf,
                                size_t buf_len) {
    uint8_t dst6[FL_NET_IPV6_ADDR_LEN];
    char resolved[64];
    const char *safe_host = (host && host[0]) ? host : "?";

    if (!buf || buf_len == 0u)
        return;
    buf[0] = '\0';
    if (resolve_dst6(host, dst6, resolved, sizeof(resolved)) != FL_RESULT_OK)
        snprintf(resolved, sizeof(resolved), "%s", safe_host);

    switch (rc) {
    case FL_RESULT_OK:
        snprintf(buf, buf_len, "ping6 %s (%s) icmpv6: ok (%.2f ms)", safe_host, resolved,
                 rtt_ms);
        break;
    case FL_RESULT_TIMEDOUT:
        snprintf(buf, buf_len, "ping6 %s (%s) icmpv6: timed out", safe_host, resolved);
        break;
    case FL_RESULT_NOSYS:
        snprintf(buf, buf_len, "ping6 %s: icmpv6 not available for this destination", safe_host);
        break;
    case FL_RESULT_INVAL:
        snprintf(buf, buf_len, "ping6: cannot resolve or invalid host %s", safe_host);
        break;
    default:
        snprintf(buf, buf_len, "ping6 %s (%s) icmpv6: failed (%d)", safe_host, resolved,
                 (int)rc);
        break;
    }
}
