#ifndef NET_DNS_H
#define NET_DNS_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_resolve_ipv4(const char *host, uint32_t *out_addr_be, char *resolved_ip,
                                size_t resolved_ip_len);

#endif /* NET_DNS_H */
