#ifndef NET_ICMPV6_H
#define NET_ICMPV6_H

#include "contract_p3_ipv6.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

size_t fl_net_icmpv6_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                        size_t payload_len);

int fl_net_icmpv6_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq);

fl_result_t fl_net_loopback_icmpv6_echo(const uint8_t *icmp_req, size_t icmp_len,
                                        const uint8_t *src6, const uint8_t *dst6,
                                        uint8_t *icmp_reply, size_t reply_cap,
                                        size_t *reply_len);

#endif /* NET_ICMPV6_H */
