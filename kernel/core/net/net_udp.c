#include "net_udp.h"

#include "contract_p3_ipv4.h"
#include "contract_result.h"
#include "net_checksum.h"
#include "net_packet.h"

#include "fl/mem_asm.h"
#include "fl/net_asm.h"

#include <string.h>

typedef struct {
    uint32_t src_ip_be;
    uint16_t src_port_host;
    uint16_t payload_len;
    uint8_t payload[FL_NET_UDP_LAB_RX_PAYLOAD_MAX];
} fl_net_udp_rx_slot_t;

typedef struct {
    uint16_t dport_host;
    unsigned bound;
    unsigned head;
    unsigned count;
    fl_net_udp_rx_slot_t queue[FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS];
} fl_net_udp_bind_entry_t;

static fl_net_udp_bind_entry_t s_udp_bind[FL_NET_UDP_BIND_SLOTS_MAX];

/** Store **host** port/length as big-endian wire octets (uses **asm_net_htons_be16**). */
static void net_udp_store_be16(uint8_t *dst, uint16_t host) {
    uint16_t n;

#if defined(FL_NET_ASM_AVAILABLE)
    n = asm_net_htons_be16(host);
#else
    n = (uint16_t)((host >> 8) | (host << 8));
#endif
    dst[0] = ((const uint8_t *)&n)[0];
    dst[1] = ((const uint8_t *)&n)[1];
}

size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len) {
    uint16_t csum;
    size_t total;

    if (!buf || payload_len > FL_NET_CONTRACT_MAX_UDP_DATAGRAM)
        return 0;
    total = (size_t)FL_NET_UDP_HDR_LEN + payload_len;
    if (cap < total)
        return 0;

    net_udp_store_be16(buf + 0, sport_host);
    net_udp_store_be16(buf + 2, dport_host);
    net_udp_store_be16(buf + 4, (uint16_t)total);
    buf[6] = 0;
    buf[7] = 0;

    if (payload_len > 0 && payload) {
#if defined(FL_NET_ASM_AVAILABLE)
        asm_mem_copy(buf + FL_NET_UDP_HDR_LEN, payload, payload_len);
#else
        memcpy(buf + FL_NET_UDP_HDR_LEN, payload, payload_len);
#endif
    }

#if defined(FL_NET_ASM_AVAILABLE)
    csum = asm_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_UDP, buf, total);
#else
    csum = fl_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_UDP, buf, total);
#endif
    buf[6] = (uint8_t)(csum >> 8);
    buf[7] = (uint8_t)(csum & 0xff);

    return total;
}

size_t fl_net_udp_build_datagram_from_pkt(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                          uint16_t sport_host, uint16_t dport_host,
                                          const fl_net_packet_t *payload_pkt) {
    const uint8_t *payload;
    size_t payload_len;

    if (!payload_pkt)
        return 0;
    if (fl_net_packet_l4_view(payload_pkt, &payload, &payload_len) != FL_RESULT_OK)
        return 0;
    return fl_net_udp_build_datagram(buf, cap, src_be, dst_be, sport_host, dport_host, payload,
                                     payload_len);
}

void fl_net_udp_demux_reset(void) {
    memset(s_udp_bind, 0, sizeof(s_udp_bind));
}

static fl_net_udp_bind_entry_t *udp_find_bound(uint16_t dport_host) {
    for (unsigned i = 0; i < FL_NET_UDP_BIND_SLOTS_MAX; i++) {
        if (s_udp_bind[i].bound && s_udp_bind[i].dport_host == dport_host)
            return &s_udp_bind[i];
    }
    return NULL;
}

static fl_net_udp_bind_entry_t *udp_alloc_bind(uint16_t dport_host) {
    fl_net_udp_bind_entry_t *existing = udp_find_bound(dport_host);
    if (existing)
        return existing;

    for (unsigned i = 0; i < FL_NET_UDP_BIND_SLOTS_MAX; i++) {
        if (!s_udp_bind[i].bound) {
            memset(&s_udp_bind[i], 0, sizeof(s_udp_bind[i]));
            s_udp_bind[i].dport_host = dport_host;
            s_udp_bind[i].bound = 1u;
            return &s_udp_bind[i];
        }
    }
    return NULL;
}

fl_result_t fl_net_udp_bind_port(uint16_t dport_host) {
    if (dport_host == 0u)
        return FL_RESULT_INVAL;
    if (!udp_alloc_bind(dport_host))
        return FL_RESULT_BUSY;
    return FL_RESULT_OK;
}

fl_result_t fl_net_udp_unbind_port(uint16_t dport_host) {
    fl_net_udp_bind_entry_t *e = udp_find_bound(dport_host);
    if (!e)
        return FL_RESULT_NOENT;
    memset(e, 0, sizeof(*e));
    return FL_RESULT_OK;
}

fl_result_t fl_net_udp_deliver_inbound_pkt(uint32_t src_ip_be, uint16_t src_port_host,
                                           uint16_t dport_host,
                                           const fl_net_packet_t *payload_pkt) {
    const uint8_t *payload;
    size_t payload_len;
    fl_result_t rc;

    if (!payload_pkt)
        return FL_RESULT_INVAL;
    rc = fl_net_packet_l4_view(payload_pkt, &payload, &payload_len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_udp_deliver_inbound(src_ip_be, src_port_host, dport_host, payload, payload_len);
}

fl_result_t fl_net_udp_deliver_inbound(uint32_t src_ip_be, uint16_t src_port_host,
                                       uint16_t dport_host, const uint8_t *payload,
                                       size_t payload_len) {
    fl_net_udp_bind_entry_t *e;
    unsigned idx;
    fl_net_udp_rx_slot_t *slot;

    if (!payload && payload_len > 0u)
        return FL_RESULT_INVAL;
    if (payload_len > FL_NET_UDP_LAB_RX_PAYLOAD_MAX)
        return FL_RESULT_INVAL;

    e = udp_find_bound(dport_host);
    if (!e)
        return FL_RESULT_NOENT;

    if (e->count >= FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS) {
        e->head = (e->head + 1u) % FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS;
        e->count--;
    }

    idx = (e->head + e->count) % FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS;
    slot = &e->queue[idx];
    slot->src_ip_be = src_ip_be;
    slot->src_port_host = src_port_host;
    slot->payload_len = (uint16_t)payload_len;
    if (payload_len > 0u)
        memcpy(slot->payload, payload, payload_len);
    e->count++;
    return FL_RESULT_OK;
}

fl_result_t fl_net_udp_recv_from_port(uint16_t dport_host, fl_net_udp_rx_meta_t *meta,
                                      uint8_t *buf, size_t cap, size_t *out_len) {
    fl_net_udp_bind_entry_t *e;
    fl_net_udp_rx_slot_t *slot;

    if (!buf || !out_len)
        return FL_RESULT_INVAL;

    e = udp_find_bound(dport_host);
    if (!e || e->count == 0u)
        return FL_RESULT_TIMEDOUT;

    slot = &e->queue[e->head];
    if (cap < slot->payload_len)
        return FL_RESULT_ERR;

    if (meta) {
        meta->src_ip_be = slot->src_ip_be;
        meta->src_port_host = slot->src_port_host;
        meta->dst_port_host = dport_host;
    }
    memcpy(buf, slot->payload, slot->payload_len);
    *out_len = slot->payload_len;
    e->head = (e->head + 1u) % FL_NET_UDP_DEFAULT_RX_QUEUE_DATAGRAMS;
    e->count--;
    return FL_RESULT_OK;
}

fl_result_t fl_net_udp_recv_from_port_pkt(uint16_t dport_host, fl_net_udp_rx_meta_t *meta,
                                          fl_net_packet_t *pkt_out, uint8_t *backing,
                                          size_t backing_cap) {
    size_t len = 0;
    fl_result_t rc;

    if (!pkt_out || !backing)
        return FL_RESULT_INVAL;

    rc = fl_net_udp_recv_from_port(dport_host, meta, backing, backing_cap, &len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_packet_bind_l4(pkt_out, backing, backing_cap, 0u, len);
}
