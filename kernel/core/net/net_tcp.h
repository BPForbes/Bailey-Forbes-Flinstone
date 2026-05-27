#ifndef NET_TCP_H
#define NET_TCP_H

#include "contract_p3_sockets.h"
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

/** Hosted stream path for **P3-7** / **P3-13** (delegates to **net_socket.c**). */
fl_result_t fl_net_tcp_stream_listen(uint32_t bind_be, uint16_t port_host,
                                     fl_net_sock_handle_t *listen_out);
fl_result_t fl_net_tcp_stream_accept(fl_net_sock_handle_t listen_h,
                                     fl_net_sock_handle_t *client_out);
fl_result_t fl_net_tcp_stream_connect(uint32_t peer_be, uint16_t port_host,
                                      fl_net_sock_handle_t *out);

#endif /* NET_TCP_H */
