#ifndef NET_WIRE_HOST_H
#define NET_WIRE_HOST_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_wire_send_icmp(uint32_t dst_be, const uint8_t *icmp, size_t icmp_len,
                                  uint8_t *rx, size_t rx_cap, size_t *rx_len,
                                  unsigned timeout_ms, double *out_rtt_ms);

fl_result_t fl_net_wire_send_tcp_syn(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                     const uint8_t *tcp, size_t tcp_len,
                                     unsigned timeout_ms, double *out_rtt_ms, char *note,
                                     size_t note_len);

fl_result_t fl_net_wire_send_udp(uint32_t dst_be, uint16_t sport, uint16_t dport,
                                 const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                 size_t rx_cap, size_t *rx_len, unsigned timeout_ms);

#endif /* NET_WIRE_HOST_H */
