#ifndef NET_PING_HOST_H
#define NET_PING_HOST_H

#include "contract_result.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** True when **addr_be** is IPv4 **127.0.0.0/8** (network byte order). */
int fl_net_ipv4_is_loopback(uint32_t addr_be);

/**
 * Hosted **P3-5** ICMP echo to **host** (dotted-quad). **count** probes (clamped 1..16).
 * On **FL_RESULT_OK**, **out_rtt_ms** receives the last RTT in milliseconds (may be NULL).
 */
fl_result_t fl_net_ping_ipv4(const char *host, unsigned count, unsigned timeout_ms,
                             double *out_rtt_ms);

#ifdef __cplusplus
}
#endif

#endif /* NET_PING_HOST_H */
