#include "net_sock_native.h"
#include "net_endian.h"

#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_tcp_fsm.h"
#include "net_wifi_netdev.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned in_use;
    unsigned is_listen;
    unsigned tcp_conn_id;
    uint32_t local_be;
    uint16_t local_port;
    uint32_t peer_be;
    uint16_t peer_port;
} fl_net_sock_native_slot_t;

static fl_net_sock_native_slot_t s_native[FL_NET_SOCK_TABLE_MAX];

static fl_net_sock_native_slot_t *native_slot(fl_net_sock_handle_t h) {
    if (h <= 0 || (unsigned)h > FL_NET_SOCK_TABLE_MAX)
        return NULL;
    return &s_native[h - 1];
}

static int driver_is_in_tree(const fl_net_driver_t *drv) {
    if (!drv)
        return 0;
    if (drv == fl_net_netdev_loopback())
        return 1;
    if (fl_net_netdev_tap() && drv == fl_net_netdev_tap())
        return 1;
    if (fl_net_wifi_netdev_driver() && drv == fl_net_wifi_netdev_driver())
        return 1;
    return 0;
}

int fl_net_sock_native_enabled(void) {
    const char *v = getenv("FL_NET_SOCK_NATIVE");
    if (!v || v[0] == '\0')
        return 0;
    if (v[0] == '0' || v[0] == 'n' || v[0] == 'N' || v[0] == 'f' || v[0] == 'F')
        return 0;
    return 1;
}

int fl_net_sock_native_eligible_peer_v4(uint32_t peer_be) {
    fl_net_route_entry_t route;
    if (!fl_net_sock_native_enabled())
        return 0;
    if (peer_be == 0u)
        return 1;
    if (fl_net_ipv4_is_loopback(peer_be))
        return 1;
    if (fl_net_route_lookup(peer_be, &route) == FL_RESULT_OK)
        return driver_is_in_tree(route.drv);
    return 0;
}

int fl_net_sock_native_eligible_bind_v4(uint32_t addr_be) {
    fl_net_route_entry_t route;
    if (!fl_net_sock_native_enabled())
        return 0;
    if (addr_be == 0u)
        return 1;
    if (fl_net_ipv4_is_loopback(addr_be))
        return 1;
    if (fl_net_route_lookup(addr_be, &route) == FL_RESULT_OK)
        return driver_is_in_tree(route.drv);
    return 0;
}

void fl_net_sock_native_slot_init(fl_net_sock_handle_t handle, fl_net_sock_type_t type) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    (void)type;
    if (!n)
        return;
    memset(n, 0, sizeof(*n));
}

int fl_net_sock_native_slot_is(const fl_net_sock_handle_t *handles, unsigned count,
                               fl_net_sock_handle_t handle) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    unsigned i;
    if (!n || !n->in_use)
        return 0;
    if (!handles)
        return 1;
    for (i = 0; i < count; i++) {
        if (handles[i] == handle)
            return 1;
    }
    return 1;
}

fl_result_t fl_net_sock_native_bind_v4(fl_net_sock_handle_t handle, uint32_t addr_be,
                                       uint16_t port_host) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    if (!n || port_host == 0u)
        return FL_RESULT_INVAL;
    if (!fl_net_sock_native_eligible_bind_v4(addr_be))
        return FL_RESULT_NOSYS;
    n->in_use = 1u;
    n->local_be = addr_be;
    n->local_port = port_host;
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_listen(fl_net_sock_handle_t handle) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    fl_result_t rc;
    if (!n || !n->in_use)
        return FL_RESULT_INVAL;
    rc = fl_net_tcp_listen_port(n->local_port);
    if (rc != FL_RESULT_OK)
        return rc;
    n->is_listen = 1u;
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_accept(fl_net_sock_handle_t listen_handle,
                                      fl_net_sock_handle_t *out_client) {
    fl_net_sock_native_slot_t *listen = native_slot(listen_handle);
    fl_net_sock_native_slot_t *client;
    unsigned conn_id;
    fl_result_t rc;
    if (!listen || !out_client || !listen->is_listen)
        return FL_RESULT_INVAL;
    rc = fl_net_tcp_accept(listen->local_port, &conn_id);
    if (rc != FL_RESULT_OK)
        return rc;
    client = native_slot(*out_client);
    if (!client)
        return FL_RESULT_INVAL;
    memset(client, 0, sizeof(*client));
    client->in_use = 1u;
    client->tcp_conn_id = conn_id;
    client->local_port = listen->local_port;
    client->local_be = listen->local_be;
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_connect_v4(fl_net_sock_handle_t handle, uint32_t local_be,
                                          uint32_t peer_be, uint16_t port_host) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    unsigned conn_id;
    fl_result_t rc;
    if (!n || port_host == 0u)
        return FL_RESULT_INVAL;
    if (!fl_net_sock_native_eligible_peer_v4(peer_be))
        return FL_RESULT_NOSYS;
    n->in_use = 1u;
    n->peer_be = peer_be;
    n->peer_port = port_host;
    n->local_be = local_be;
    rc = fl_net_tcp_connect_src(local_be, peer_be, port_host, &conn_id);
    if (rc != FL_RESULT_OK) {
        n->in_use = 0u;
        return rc;
    }
    n->tcp_conn_id = conn_id;
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_send(fl_net_sock_handle_t handle, const void *buf, size_t len,
                                    size_t *sent) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    fl_result_t rc;
    if (!n || !n->in_use || n->is_listen)
        return FL_RESULT_INVAL;
    if (!buf || !sent)
        return FL_RESULT_INVAL;
    rc = fl_net_tcp_send(n->tcp_conn_id, (const uint8_t *)buf, len);
    if (rc != FL_RESULT_OK)
        return rc;
    *sent = len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_recv(fl_net_sock_handle_t handle, void *buf, size_t cap,
                                    size_t *got, unsigned timeout_ms) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    fl_result_t rc;
    size_t nread;
    unsigned spins = 0u;
    unsigned max_spins = timeout_ms > 0u ? (timeout_ms / 10u) + 1u : 1u;
    if (!n || !n->in_use || n->is_listen)
        return FL_RESULT_INVAL;
    if (!buf || !got)
        return FL_RESULT_INVAL;
    for (;;) {
        rc = fl_net_tcp_recv(n->tcp_conn_id, (uint8_t *)buf, cap, &nread);
        if (rc == FL_RESULT_OK) {
            *got = nread;
            return FL_RESULT_OK;
        }
        if (rc != FL_RESULT_TIMEDOUT)
            return rc;
        spins++;
        if (spins >= max_spins)
            return FL_RESULT_TIMEDOUT;
    }
}

fl_result_t fl_net_sock_native_close(fl_net_sock_handle_t handle) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    if (!n || !n->in_use)
        return FL_RESULT_INVAL;
    if (!n->is_listen)
        (void)fl_net_tcp_close(n->tcp_conn_id);
    memset(n, 0, sizeof(*n));
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_local_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    if (!n || !n->in_use || !out_be)
        return FL_RESULT_INVAL;
    if (n->local_be != 0u) {
        *out_be = n->local_be;
        return FL_RESULT_OK;
    }
    {
        fl_net_route_entry_t route;
        if (n->peer_be != 0u &&
            fl_net_route_lookup(n->peer_be, &route) == FL_RESULT_OK) {
            *out_be = route.src_ip_be;
            return FL_RESULT_OK;
        }
    }
    *out_be = fl_net_htonl(0x7F000001u);
    return FL_RESULT_OK;
}

fl_result_t fl_net_sock_native_peer_ipv4(fl_net_sock_handle_t handle, uint32_t *out_be) {
    fl_net_sock_native_slot_t *n = native_slot(handle);
    if (!n || !n->in_use || !out_be)
        return FL_RESULT_INVAL;
    if (n->peer_be == 0u)
        return FL_RESULT_NOENT;
    *out_be = n->peer_be;
    return FL_RESULT_OK;
}
