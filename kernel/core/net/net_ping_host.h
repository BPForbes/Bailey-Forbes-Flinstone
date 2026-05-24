#ifndef NET_PING_HOST_H
#define NET_PING_HOST_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True when **addr_be** is IPv4 **127.0.0.0/8** (network byte order). */
int fl_net_ipv4_is_loopback(uint32_t addr_be);

/**
 * Resolve **host** (IPv4 literal or DNS name) to **AF_INET**. On success, writes
 * **out** and dotted-quad **resolved_ip** (at least **INET_ADDRSTRLEN** bytes).
 */
fl_result_t fl_net_resolve_ipv4(const char *host, uint32_t *out_addr_be,
                                char *resolved_ip, size_t resolved_ip_len);

/**
 * Probe host:port with real I/O (no canned or mock targets).
 * port 0: ICMP echo (Linux ping socket or system ping when unprivileged).
 * port 1-65535: TCP connect RTT to that port on the resolved address.
 * count applies to ICMP only (clamped 1..16); TCP performs one connect.
 */
fl_result_t fl_net_ping(const char *host, uint16_t port, unsigned count,
                        unsigned timeout_ms, double *out_rtt_ms);

/** Human-readable outcome for **fl_net_ping** (TCP vs ICMP, connect/refused/timeout). */
void fl_net_ping_format_result(const char *host, uint16_t port, fl_result_t rc,
                               double rtt_ms, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* NET_PING_HOST_H */
