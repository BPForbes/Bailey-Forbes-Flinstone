#ifndef NET_ICMP_H
#define NET_ICMP_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_ICMP_ECHO_PAYLOAD_DEFAULT 32u

size_t fl_net_icmp_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                      size_t payload_len);

int fl_net_icmp_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq);

fl_result_t fl_net_icmp_echo_exchange(uint32_t dst_be, uint16_t id, uint16_t seq,
                                      unsigned timeout_ms, double *out_rtt_ms);

#endif /* NET_ICMP_H */
