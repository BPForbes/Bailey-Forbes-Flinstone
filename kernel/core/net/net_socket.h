#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include "contract_result.h"
#include "contract_p3_sockets.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_sock_init(void);
void fl_net_sock_shutdown(void);

fl_result_t fl_net_sock_open(fl_net_sock_type_t type, fl_net_sock_handle_t *out_handle);
fl_result_t fl_net_sock_close(fl_net_sock_handle_t handle);

fl_result_t fl_net_sock_bind(fl_net_sock_handle_t handle, uint32_t addr_be, uint16_t port_host);
fl_result_t fl_net_sock_listen(fl_net_sock_handle_t handle, int backlog);
fl_result_t fl_net_sock_accept(fl_net_sock_handle_t listen_handle,
                               fl_net_sock_handle_t *out_client);

fl_result_t fl_net_sock_connect(fl_net_sock_handle_t handle, uint32_t peer_be,
                                uint16_t port_host);

fl_result_t fl_net_sock_send(fl_net_sock_handle_t handle, const void *buf, size_t len,
                             size_t *sent);
fl_result_t fl_net_sock_recv(fl_net_sock_handle_t handle, void *buf, size_t cap, size_t *got,
                             unsigned timeout_ms);

/** Set non-blocking mode when **nonblock** is non-zero (hosted path). */
fl_result_t fl_net_sock_set_nonblock(fl_net_sock_handle_t handle, int nonblock);

#endif /* NET_SOCKET_H */
