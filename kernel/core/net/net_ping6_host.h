#ifndef NET_PING6_HOST_H
#define NET_PING6_HOST_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Resolve **host** (literal or DNS AAAA) and ICMPv6 echo on loopback **::1** (#303). */
fl_result_t fl_net_ping6(const char *host, unsigned count, unsigned timeout_ms,
                         double *out_rtt_ms);

void fl_net_ping6_format_result(const char *host, fl_result_t rc, double rtt_ms, char *buf,
                                size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* NET_PING6_HOST_H */
