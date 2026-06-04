#ifndef NET_TCP_FSM_H
#define NET_TCP_FSM_H

#include "contract_p3_tcp.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#ifndef FL_NET_TCP_FSM_CONN_MAX
#define FL_NET_TCP_FSM_CONN_MAX 8u
#endif

#ifndef FL_NET_TCP_FSM_LISTEN_MAX
#define FL_NET_TCP_FSM_LISTEN_MAX 4u
#endif

#ifndef FL_NET_TCP_FSM_RX_CAP
#define FL_NET_TCP_FSM_RX_CAP 512u
#endif

void fl_net_tcp_fsm_reset(void);

fl_result_t fl_net_tcp_listen_port(uint16_t port_host);

fl_result_t fl_net_tcp_connect(uint32_t dst_be, uint16_t dport_host, unsigned *conn_id_out);

/** Like **fl_net_tcp_connect** but binds the local IPv4 source when **src_be** is non-zero. */
fl_result_t fl_net_tcp_connect_src(uint32_t src_be, uint32_t dst_be, uint16_t dport_host,
                                   unsigned *conn_id_out);

/** Non-blocking: **FL_RESULT_OK** when a connection is ready; **FL_RESULT_TIMEDOUT** if none. */
fl_result_t fl_net_tcp_accept(uint16_t listen_port_host, unsigned *conn_id_out);

fl_result_t fl_net_tcp_send(unsigned conn_id, const uint8_t *data, size_t len);

fl_result_t fl_net_tcp_recv(unsigned conn_id, uint8_t *buf, size_t cap, size_t *out_len);

fl_result_t fl_net_tcp_close(unsigned conn_id);

/**
 * Loopback IPv4 TCP input (**P3-7**). Writes reply segment to **reply** when needed;
 * returns **FL_RESULT_OK** with **reply_len** 0 when absorbed without reply.
 */
fl_result_t fl_net_tcp_loopback_input(uint32_t src_be, uint32_t dst_be, const uint8_t *tcp,
                                      size_t tcp_len, uint8_t *reply, size_t reply_cap,
                                      size_t *reply_len);

#endif /* NET_TCP_FSM_H */
