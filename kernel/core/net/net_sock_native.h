#ifndef NET_SOCK_NATIVE_H
#define NET_SOCK_NATIVE_H

#include "contract_result.h"
#include "net_endpoint.h"
#include "net_socket.h"

#include <stddef.h>
#include <stdint.h>

/** In-tree TCP (P3-7 FSM + wire egress) instead of POSIX when eligible. */
int fl_net_sock_native_enabled(void);

int fl_net_sock_native_eligible_bind_v4(uint32_t addr_be);
int fl_net_sock_native_eligible_peer_v4(uint32_t peer_be);

void fl_net_sock_native_slot_init(fl_net_sock_handle_t handle, fl_net_sock_type_t type);
int fl_net_sock_native_slot_is(const fl_net_sock_handle_t *handles, unsigned count,
                               fl_net_sock_handle_t handle);

fl_result_t fl_net_sock_native_bind_v4(fl_net_sock_handle_t handle, uint32_t addr_be,
                                       uint16_t port_host);
fl_result_t fl_net_sock_native_listen(fl_net_sock_handle_t handle);
fl_result_t fl_net_sock_native_accept(fl_net_sock_handle_t listen_handle,
                                      fl_net_sock_handle_t *out_client);
fl_result_t fl_net_sock_native_connect_v4(fl_net_sock_handle_t handle, uint32_t local_be,
                                          uint32_t peer_be, uint16_t port_host);
fl_result_t fl_net_sock_native_send(fl_net_sock_handle_t handle, const void *buf, size_t len,
                                    size_t *sent);
fl_result_t fl_net_sock_native_recv(fl_net_sock_handle_t handle, void *buf, size_t cap,
                                    size_t *got, unsigned timeout_ms);
fl_result_t fl_net_sock_native_close(fl_net_sock_handle_t handle);

fl_result_t fl_net_sock_native_local_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be);
fl_result_t fl_net_sock_native_peer_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be);

void fl_net_sock_native_pump(unsigned max_frames);

#endif /* NET_SOCK_NATIVE_H */
