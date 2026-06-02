#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include "contract_result.h"
#include "contract_p3_sockets.h"
#include "net_endpoint.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_sock_init(void);
void fl_net_sock_shutdown(void);

fl_result_t fl_net_sock_open(fl_net_sock_type_t type, fl_net_sock_handle_t *out_handle);
fl_result_t fl_net_sock_open_for(const fl_net_endpoint_t *endpoint_hint,
                                   fl_net_sock_type_t type,
                                   fl_net_sock_handle_t *out_handle);
fl_result_t fl_net_sock_close(fl_net_sock_handle_t handle);

fl_result_t fl_net_sock_bind(fl_net_sock_handle_t handle, uint32_t addr_be, uint16_t port_host);
fl_result_t fl_net_sock_bind_ep(fl_net_sock_handle_t handle, const fl_net_endpoint_t *local);
fl_result_t fl_net_sock_listen(fl_net_sock_handle_t handle, int backlog);
fl_result_t fl_net_sock_accept(fl_net_sock_handle_t listen_handle,
                               fl_net_sock_handle_t *out_client);

fl_result_t fl_net_sock_connect(fl_net_sock_handle_t handle, uint32_t peer_be,
                                uint16_t port_host);

/**
 * Same as `fl_net_sock_connect` but binds the **local source** address to
 * `local_be:0` (ephemeral port) before connecting. Used by the multi-IP
 * server demo / tests so each client sources its TCP from a distinct
 * 10.99.0.X loopback alias instead of the default 0.0.0.0 → 127.0.0.1.
 * Pass `local_be == 0` to behave like the plain `fl_net_sock_connect`.
 */
fl_result_t fl_net_sock_connect_from(fl_net_sock_handle_t handle,
                                     uint32_t local_be,
                                     uint32_t peer_be, uint16_t port_host);
fl_result_t fl_net_sock_connect_from_ep(fl_net_sock_handle_t handle,
                                        const fl_net_endpoint_t *local,
                                        const fl_net_endpoint_t *peer);

/** Hosted-only helper: write the connected peer or local IPv4 into
 * `*out_be`. Returns FL_RESULT_OK on success, FL_RESULT_NOSYS / INVAL
 * otherwise. */
fl_result_t fl_net_sock_peer_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be);
fl_result_t fl_net_sock_local_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be);
fl_result_t fl_net_sock_peer_endpoint(fl_net_sock_handle_t handle, fl_net_endpoint_t *out);
fl_result_t fl_net_sock_local_endpoint(fl_net_sock_handle_t handle, fl_net_endpoint_t *out);

fl_result_t fl_net_sock_send(fl_net_sock_handle_t handle, const void *buf, size_t len,
                             size_t *sent);
fl_result_t fl_net_sock_recv(fl_net_sock_handle_t handle, void *buf, size_t cap, size_t *got,
                             unsigned timeout_ms);

/** Set non-blocking mode when **nonblock** is non-zero (hosted path). */
fl_result_t fl_net_sock_set_nonblock(fl_net_sock_handle_t handle, int nonblock);

/** Hosted BSD socket fd, or **-1** when unavailable (**#252** TLS bridge). */
int fl_net_sock_host_fd(fl_net_sock_handle_t handle);

#endif /* NET_SOCKET_H */
