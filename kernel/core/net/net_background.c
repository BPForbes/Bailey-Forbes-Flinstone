#include "net_background.h"

#include "fl/ipc.h"
#include "fl/mem_asm.h"

#include "workqueue.h"

#include "contract_p3_ipv4.h"
#include "contract_p3_loopback.h"
#include "net_packet.h"
#include "net_route.h"
#include "net_arp.h"
#include "net_packet.h"
#include "net_udp.h"
#include "net_wire_egress.h"
#include "net_wire_host.h"

#include "fl/net_asm.h"

#include <pthread.h>

typedef struct {
    uint16_t payload_len;
    uint8_t payload[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
} fl_net_task_backend_inbox_msg_t;

_Static_assert(sizeof(fl_net_task_backend_inbox_msg_t) > 2u,
               "inbox msg must include at least len field");

static msgq_t *s_user_inboxes[FL_NET_TASK_BACKEND_MAX_USERS];
static unsigned s_user_inbox_msg_size;
static pthread_mutex_t s_user_inboxes_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    uint32_t peer_ip_be;
    uint16_t peer_port_host;
    unsigned bound;
} fl_net_task_backend_peer_t;

static fl_net_task_backend_peer_t s_peers[FL_NET_TASK_BACKEND_MAX_USERS];
static uint32_t s_hub_ip_be;
static uint16_t s_hub_port_host;
static unsigned s_hub_bound;

#ifndef FL_NET_ARP_CACHE_STALE_TICKS
#define FL_NET_ARP_CACHE_STALE_TICKS 512u
#endif

static void net_bg_work(void *ctx) {
    (void)ctx;
    /* P3-14: ARP cache TTL sweep (**#240**). TCP timer wheel / RX dequeue remain future. */
    (void)fl_net_arp_cache_sweep(FL_NET_ARP_CACHE_STALE_TICKS);
}

static int s_net_bg_inited;

static unsigned user_slot_sanity(unsigned slot) {
    return slot < FL_NET_TASK_BACKEND_MAX_USERS ? slot : (unsigned)-1;
}

void fl_net_background_init(void) {
    s_net_bg_inited = 1;

    /* msgq backend state lives outside inbox registration; initialize globals. */
    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++) {
        s_user_inboxes[i] = NULL;
        s_peers[i].bound = 0u;
    }
    s_user_inbox_msg_size = (unsigned)sizeof(fl_net_task_backend_inbox_msg_t);
    s_hub_bound = 0u;
}

void fl_net_background_shutdown(void) {
    s_net_bg_inited = 0;

    pthread_mutex_lock(&s_user_inboxes_mu);
    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++) {
        if (s_user_inboxes[i]) {
            msgq_destroy(s_user_inboxes[i]);
            s_user_inboxes[i] = NULL;
        }
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
}

fl_result_t fl_net_background_arp_tick_kick(void) {
    fl_wq_work_t w = {
        .fn = net_bg_work,
        .ctx = NULL,
        .tag = FL_NET_BG_TAG_ARP_TICK,
        .pq_layer = FL_WQ_LAYER_BACKGROUND,
    };

    if (!s_net_bg_inited)
        fl_net_background_init();
    return fl_wq_enqueue(fl_wq_default(), &w);
}

void fl_net_background_tick(unsigned max_items) {
    (void)max_items;
    if (!s_net_bg_inited)
        fl_net_background_init();

    /* TODO: P3-14 — do not recv from fl_net_netdev_loopback() here; that queue is
     * shared with ICMP/TCP probes (net_wire_egress). Draining it steals replies.
     *
     * Distribution: socket + ARP xmit APIs below.
     * TODO: P3-13 — wire RX demux calls fl_net_task_backend_server_ingress().
     */
}

fl_result_t fl_net_task_backend_peer_bind(unsigned slot, uint32_t peer_ip_be, uint16_t port_host) {
    if (!s_net_bg_inited)
        fl_net_background_init();

    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1 || port_host == 0u)
        return FL_RESULT_INVAL;

    pthread_mutex_lock(&s_user_inboxes_mu);
    s_peers[slot].peer_ip_be = peer_ip_be;
    s_peers[slot].peer_port_host = port_host;
    s_peers[slot].bound = 1u;
    pthread_mutex_unlock(&s_user_inboxes_mu);
    return FL_RESULT_OK;
}

fl_result_t fl_net_task_backend_hub_bind(uint32_t hub_ip_be, uint16_t hub_port_host) {
    if (!s_net_bg_inited)
        fl_net_background_init();
    if (hub_port_host == 0u)
        hub_port_host = (uint16_t)FL_NET_TASK_BACKEND_WIRE_SERVER_PORT;

    pthread_mutex_lock(&s_user_inboxes_mu);
    s_hub_ip_be = hub_ip_be;
    s_hub_port_host = hub_port_host;
    s_hub_bound = 1u;
    pthread_mutex_unlock(&s_user_inboxes_mu);
    return FL_RESULT_OK;
}

static uint16_t port_host_to_be16(uint16_t port_host) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_htons_be16(port_host);
#else
    return (uint16_t)((port_host >> 8) | (port_host << 8));
#endif
}

static uint16_t port_be_to_host16(uint16_t port_be) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_htons_be16(port_be);
#else
    return (uint16_t)((port_be >> 8) | (port_be << 8));
#endif
}

static fl_result_t task_backend_resolve_local_ip(uint32_t peer_ip_be, uint32_t *local_ip_be) {
    fl_net_route_entry_t route;

    if (!local_ip_be)
        return FL_RESULT_INVAL;

    if (fl_net_route_lookup(peer_ip_be, &route) == FL_RESULT_OK && route.src_ip_be != 0u) {
        *local_ip_be = route.src_ip_be;
        return FL_RESULT_OK;
    }

    if (s_hub_bound) {
        *local_ip_be = s_hub_ip_be;
        return FL_RESULT_OK;
    }

    *local_ip_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
    return FL_RESULT_OK;
}

static fl_result_t task_backend_xmit_udp_socket_pkt(const fl_net_socket_endpoint_t *endpoint,
                                                    const fl_net_packet_t *payload_pkt) {
    uint8_t udp[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX + FL_NET_UDP_HDR_LEN];
    fl_net_packet_t udp_pkt;
    size_t udp_len;
    fl_result_t rc;

    udp_len = fl_net_udp_build_datagram_from_pkt(
        udp, sizeof(udp), endpoint->local_ip_be, endpoint->peer_ip_be,
        port_be_to_host16(endpoint->local_port_be), port_be_to_host16(endpoint->peer_port_be),
        payload_pkt);
    if (udp_len == 0)
        return FL_RESULT_ERR;

    rc = fl_net_packet_bind_l4(&udp_pkt, udp, sizeof(udp), 0u, udp_len);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_wire_egress_l4_xmit_pkt(endpoint->peer_ip_be, FL_NET_IP_PROTO_UDP, &udp_pkt,
                                        FL_NET_TASK_BACKEND_ARP_TIMEOUT_MS);
    if (rc == FL_RESULT_OK)
        return FL_RESULT_OK;

    if (rc != FL_RESULT_NOENT)
        return rc;

    {
        uint8_t rx_dummy[64];
        size_t rx_len = 0;
        fl_net_packet_t rx_pkt;
        uint16_t sport = port_be_to_host16(endpoint->local_port_be);
        uint16_t dport = port_be_to_host16(endpoint->peer_port_be);

        rc = fl_net_wire_send_udp_pkt(endpoint->peer_ip_be, sport, dport, payload_pkt, &rx_pkt,
                                      rx_dummy, sizeof(rx_dummy), &rx_len, 1u);
        if (rc == FL_RESULT_TIMEDOUT)
            return FL_RESULT_OK;
    }
    return rc;
}

fl_result_t fl_net_task_backend_socket_send(const fl_net_socket_endpoint_t *endpoint,
                                            const uint8_t *payload, size_t payload_len) {
    fl_net_packet_t payload_pkt;
    fl_net_socket_endpoint_t ep;
    fl_result_t rc;

    if (!endpoint || !payload || payload_len == 0u)
        return FL_RESULT_INVAL;
    if (payload_len > FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX)
        return FL_RESULT_INVAL;

    rc = fl_net_packet_bind_l4(&payload_pkt, (uint8_t *)(uintptr_t)payload, payload_len, 0u,
                               payload_len);
    if (rc != FL_RESULT_OK)
        return rc;

    ep = *endpoint;
    return task_backend_xmit_udp_socket_pkt(&ep, &payload_pkt);
}

fl_result_t fl_net_task_backend_user_open(unsigned slot, unsigned max_inbox_messages) {
    fl_net_task_backend_inbox_msg_t msg_tmp;

    if (!s_net_bg_inited)
        fl_net_background_init();

    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1)
        return FL_RESULT_INVAL;
    if (max_inbox_messages == 0u)
        max_inbox_messages = 4u;

    pthread_mutex_lock(&s_user_inboxes_mu);
    if (s_user_inboxes[slot]) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_OK;
    }

    /* msgq_create requires fixed message_size. */
    s_user_inbox_msg_size = (unsigned)sizeof(msg_tmp);
    msgq_t *q = msgq_create(max_inbox_messages, (size_t)s_user_inbox_msg_size);
    if (!q) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOMEM;
    }

    s_user_inboxes[slot] = q;
    pthread_mutex_unlock(&s_user_inboxes_mu);
    return FL_RESULT_OK;
}

void fl_net_task_backend_user_close(unsigned slot) {
    slot = user_slot_sanity(slot);
    if (slot == (unsigned)-1)
        return;
    pthread_mutex_lock(&s_user_inboxes_mu);
    if (s_user_inboxes[slot]) {
        msgq_destroy(s_user_inboxes[slot]);
        s_user_inboxes[slot] = NULL;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
}

fl_result_t fl_net_task_backend_send_packet(unsigned dst_slot, const fl_net_packet_t *pkt) {
    fl_net_task_backend_inbox_msg_t msg;

    if (!s_net_bg_inited)
        fl_net_background_init();

    dst_slot = user_slot_sanity(dst_slot);
    if (dst_slot == (unsigned)-1 || !pkt)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_user_inboxes_mu);
    msgq_t *q = s_user_inboxes[dst_slot];
    if (q == NULL) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOENT;
    }

    if ((pkt->valid & FL_NET_PKT_VALID_L4) == 0u) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }
    if (pkt->l4.len > FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }

    asm_mem_zero(&msg, sizeof(msg));
    msg.payload_len = (uint16_t)pkt->l4.len;

    {
        size_t out_len = 0;
        fl_result_t rc = fl_net_packet_copy_l4(pkt, msg.payload, sizeof(msg.payload), &out_len);
        if (rc != FL_RESULT_OK) {
            pthread_mutex_unlock(&s_user_inboxes_mu);
            return rc;
        }
        if (out_len != pkt->l4.len) {
            pthread_mutex_unlock(&s_user_inboxes_mu);
            return FL_RESULT_ERR;
        }
    }

    if (msgq_send(q, &msg, (size_t)sizeof(msg)) != 0) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_BUSY;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);
    return FL_RESULT_OK;
}

fl_result_t fl_net_task_backend_recv_packet(unsigned src_slot, uint8_t *out, size_t cap,
                                              size_t *out_len) {
    fl_net_task_backend_inbox_msg_t msg;

    if (!s_net_bg_inited)
        fl_net_background_init();

    src_slot = user_slot_sanity(src_slot);
    if (src_slot == (unsigned)-1 || !out || !out_len)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s_user_inboxes_mu);
    msgq_t *q = s_user_inboxes[src_slot];
    if (q == NULL) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_NOENT;
    }

    if (cap == 0u) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_INVAL;
    }

    if (msgq_receive(q, &msg, (size_t)sizeof(msg), 0u) != 0) {
        pthread_mutex_unlock(&s_user_inboxes_mu);
        return FL_RESULT_TIMEDOUT;
    }
    pthread_mutex_unlock(&s_user_inboxes_mu);

    if (msg.payload_len > cap)
        return FL_RESULT_INVAL;

    asm_mem_copy(out, msg.payload, (size_t)msg.payload_len);
    *out_len = (size_t)msg.payload_len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_task_backend_client_wire_send(uint32_t server_addr_be, uint16_t client_sport,
                                                 uint16_t server_port,
                                                 const fl_net_packet_t *pkt) {
    fl_net_socket_endpoint_t ep;
    uint8_t payload[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
    size_t payload_len = 0;
    fl_result_t rc;
    uint32_t local_ip_be;

    if (!pkt)
        return FL_RESULT_INVAL;
    if (server_port == 0u)
        server_port = (uint16_t)FL_NET_TASK_BACKEND_WIRE_SERVER_PORT;

    rc = fl_net_packet_copy_l4(pkt, payload, sizeof(payload), &payload_len);
    if (rc != FL_RESULT_OK)
        return rc;
    if (payload_len == 0u)
        return FL_RESULT_INVAL;

    rc = task_backend_resolve_local_ip(server_addr_be, &local_ip_be);
    if (rc != FL_RESULT_OK)
        return rc;

    ep.local_ip_be = local_ip_be;
    ep.local_port_be = port_host_to_be16(client_sport);
    ep.peer_ip_be = server_addr_be;
    ep.peer_port_be = port_host_to_be16(server_port);

    return fl_net_task_backend_socket_send(&ep, payload, payload_len);
}

fl_result_t fl_net_task_backend_server_relay_to_clients(unsigned src_slot,
                                                        const fl_net_packet_t *pkt) {
    unsigned delivered = 0;
    fl_result_t last_err = FL_RESULT_NOENT;
    uint8_t payload[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
    size_t payload_len = 0;
    fl_result_t copy_rc;

    if (!pkt)
        return FL_RESULT_INVAL;

    src_slot = user_slot_sanity(src_slot);
    if (src_slot == (unsigned)-1)
        return FL_RESULT_INVAL;

    copy_rc = fl_net_packet_copy_l4(pkt, payload, sizeof(payload), &payload_len);
    if (copy_rc != FL_RESULT_OK)
        return copy_rc;

    for (unsigned i = 0; i < FL_NET_TASK_BACKEND_MAX_USERS; i++) {
        fl_result_t rc;
        fl_net_socket_endpoint_t ep;
        uint32_t hub_ip;
        unsigned peer_bound;
        uint32_t peer_ip;
        uint16_t peer_port;

        if (i == src_slot)
            continue;

        rc = fl_net_task_backend_send_packet(i, pkt);
        if (rc == FL_RESULT_OK)
            delivered++;
        else if (rc != FL_RESULT_NOENT)
            last_err = rc;

        pthread_mutex_lock(&s_user_inboxes_mu);
        peer_bound = s_peers[i].bound;
        peer_ip = s_peers[i].peer_ip_be;
        peer_port = s_peers[i].peer_port_host;
        hub_ip = s_hub_bound ? s_hub_ip_be : (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
        pthread_mutex_unlock(&s_user_inboxes_mu);

        if (!peer_bound || !s_hub_bound)
            continue;

        ep.local_ip_be = hub_ip;
        ep.local_port_be = port_host_to_be16(s_hub_port_host);
        ep.peer_ip_be = peer_ip;
        ep.peer_port_be = port_host_to_be16(peer_port);

        rc = fl_net_task_backend_socket_send(&ep, payload, payload_len);
        if (rc == FL_RESULT_OK)
            delivered++;
        else if (rc != FL_RESULT_NOENT)
            last_err = rc;
    }

    if (delivered > 0)
        return FL_RESULT_OK;
    return last_err;
}

fl_result_t fl_net_task_backend_server_ingress(unsigned from_client_slot,
                                               const fl_net_packet_t *pkt) {
    /* TODO: P3-13 — map wire source (port/session) to from_client_slot; validate
     * UDP/TCP L4 length before routing (skip when l4.len < 4 for port demux).
     */
    return fl_net_task_backend_server_relay_to_clients(from_client_slot, pkt);
}
