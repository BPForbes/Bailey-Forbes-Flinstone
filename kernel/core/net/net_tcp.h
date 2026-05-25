#ifndef NET_TCP_H
#define NET_TCP_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_TCP_FLAG_SYN 0x02u
#define FL_NET_TCP_FLAG_ACK 0x10u
#define FL_NET_TCP_FLAG_RST 0x04u

size_t fl_net_tcp_build_syn(uint8_t *buf, size_t cap, uint16_t sport, uint16_t dport,
                            uint32_t seq_be);

fl_result_t fl_net_tcp_syn_probe(uint32_t dst_be, uint16_t dport, unsigned timeout_ms,
                                 double *out_rtt_ms, char *note, size_t note_len);

#endif /* NET_TCP_H */
