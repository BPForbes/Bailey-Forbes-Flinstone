#ifndef NET_LOOPBACK_H
#define NET_LOOPBACK_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

int fl_net_loopback_owns(uint32_t dst_be);

fl_result_t fl_net_loopback_icmp_echo(const uint8_t *icmp_req, size_t icmp_len,
                                      uint8_t *icmp_reply, size_t reply_cap,
                                      size_t *reply_len);

fl_result_t fl_net_loopback_tcp_syn(const uint8_t *tcp_syn, size_t tcp_len, uint16_t dport,
                                    uint8_t *tcp_reply, size_t reply_cap, size_t *reply_len);

#endif /* NET_LOOPBACK_H */
