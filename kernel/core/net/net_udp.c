#include "net_udp.h"

#include "contract_p3_ipv4.h"
#include "contract_result.h"
#include "net_checksum.h"
#include "net_packet.h"
#include "net_ipv4.h"
#include "net_route.h"
#include "net_wire_egress.h"
#include "net_endian.h"

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

static uint16_t net_udp_read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static fl_net_udp_bind_entry_t *udp_find_bound(uint16_t dport_host);

/** Store **host** port/length as big-endian wire octets (#284: fl_net_htons). */
static void net_udp_store_be16(uint8_t *dst, uint16_t host) {
    fl_net_put_u16_be(dst, host);
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

unsigned fl_net_udp_bound_ports_snapshot(uint16_t *out, unsigned cap) {
    unsigned written = 0u;
    if (!out)
        return 0u;
    for (unsigned i = 0; i < FL_NET_UDP_BIND_SLOTS_MAX && written < cap; i++) {
        if (s_udp_bind[i].bound)
            out[written++] = s_udp_bind[i].dport_host;
    }
    return written;
}

static fl_net_udp_bind_entry_t *udp_find_bound(uint16_t dport_host) {
    for (unsigned i = 0; i < FL_NET_UDP_BIND_SLOTS_MAX; i++) {
        if (s_udp_bind[i].bound && s_udp_bind[i].dport_host == dport_host)
            return &s_udp_bind[i];
    }
    return NULL;
}


fl_result_t fl_net_udp_parse(const uint8_t *udp, size_t len, uint32_t src_be, uint32_t dst_be,
                             int verify_csum, fl_net_udp_parsed_t *out) {
    uint16_t ulen;

    if (!udp || !out)
        return FL_RESULT_INVAL;
    if (len < FL_NET_UDP_HDR_LEN)
        return FL_RESULT_INVAL;

    ulen = net_udp_read_be16(udp + 4);
    if (ulen < FL_NET_UDP_HDR_LEN || (size_t)ulen > len)
        return FL_RESULT_ERR;

    if (verify_csum && !fl_net_udp_checksum_valid(src_be, dst_be, udp, (size_t)ulen))
        return FL_RESULT_ERR;

    out->sport_host = net_udp_read_be16(udp + 0);
    out->dport_host = net_udp_read_be16(udp + 2);
    out->length_host = ulen;
    out->payload = udp + FL_NET_UDP_HDR_LEN;
    out->payload_len = (size_t)ulen - FL_NET_UDP_HDR_LEN;
    return FL_RESULT_OK;
}

fl_result_t fl_net_udp_xmit_pkt(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                                const fl_net_packet_t *payload_pkt, unsigned arp_timeout_ms) {
    uint8_t l4[FL_NET_UDP_HDR_LEN + FL_NET_UDP_LAB_RX_PAYLOAD_MAX];
    uint32_t src_be;
    size_t l4_len;
    fl_net_packet_t udp_pkt;
    fl_result_t rc;

    if (!payload_pkt)
        return FL_RESULT_INVAL;
    if (fl_net_ipv4_is_loopback(dst_be)) {
        src_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
    } else {
        fl_net_route_entry_t route;

        if (fl_net_route_lookup(dst_be, &route) != FL_RESULT_OK)
            return FL_RESULT_NOENT;
        src_be = route.src_ip_be;
    }

    l4_len = fl_net_udp_build_datagram_from_pkt(l4, sizeof(l4), src_be, dst_be, sport_host,
                                                dport_host, payload_pkt);
    if (l4_len == 0)
        return FL_RESULT_ERR;

    rc = fl_net_packet_bind_l4(&udp_pkt, l4, sizeof(l4), 0u, l4_len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_wire_egress_l4_xmit_pkt(dst_be, FL_NET_IP_PROTO_UDP, &udp_pkt, arp_timeout_ms);
}

fl_result_t fl_net_udp_xmit(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                            const uint8_t *payload, size_t payload_len, unsigned arp_timeout_ms) {
    fl_net_packet_t pkt;

    if (!payload && payload_len > 0u)
        return FL_RESULT_INVAL;
    if (fl_net_packet_bind_l4(&pkt, (uint8_t *)(uintptr_t)payload, payload_len, 0u,
                              payload_len) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return fl_net_udp_xmit_pkt(dst_be, sport_host, dport_host, &pkt, arp_timeout_ms);
}

fl_result_t fl_net_udp_echo_exchange(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                                     const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                     size_t rx_cap, size_t *rx_len, unsigned timeout_ms) {
    uint8_t l4[FL_NET_UDP_HDR_LEN + FL_NET_UDP_LAB_RX_PAYLOAD_MAX];
    uint32_t src_be;
    size_t l4_len;
    fl_net_packet_t tx_pkt;
    fl_result_t rc;

    if (!rx || !rx_len)
        return FL_RESULT_INVAL;
    if (!udp_find_bound(sport_host))
        return FL_RESULT_NOENT;
    if (fl_net_ipv4_is_loopback(dst_be)) {
        src_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
    } else {
        fl_net_route_entry_t route;

        if (fl_net_route_lookup(dst_be, &route) != FL_RESULT_OK)
            return FL_RESULT_NOENT;
        src_be = route.src_ip_be;
    }

    l4_len = fl_net_udp_build_datagram(l4, sizeof(l4), src_be, dst_be, sport_host, dport_host,
                                       payload, payload_len);
    if (l4_len == 0)
        return FL_RESULT_ERR;

    rc = fl_net_packet_bind_l4(&tx_pkt, l4, sizeof(l4), 0u, l4_len);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = fl_net_wire_egress_l4_pkt(dst_be, FL_NET_IP_PROTO_UDP, &tx_pkt, rx, rx_cap, rx_len,
                                     timeout_ms, NULL);
    if (rc != FL_RESULT_OK)
        return rc;

    {
        fl_net_udp_parsed_t parsed;

        if (fl_net_udp_parse(rx, *rx_len, dst_be, src_be, 0, &parsed) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        if (parsed.payload_len > rx_cap)
            return FL_RESULT_ERR;
        memmove(rx, parsed.payload, parsed.payload_len);
        *rx_len = parsed.payload_len;
    }
    return FL_RESULT_OK;
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
