#include "net_tcp_fsm.h"

#include "contract_p3_ipv4.h"
#include "contract_p3_udp.h"
#include "net_checksum.h"
#include "net_packet.h"
#include "fl/net_asm.h"
#include "net_ipv4.h"
#include "net_route.h"
#include "net_wire_egress.h"

#include <string.h>

typedef struct {
    unsigned in_use;
    fl_net_tcp_state_t state;
    uint32_t local_ip_be;
    uint32_t remote_ip_be;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t iss;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    unsigned accept_ready;
    uint8_t rx_buf[FL_NET_TCP_FSM_RX_CAP];
    size_t rx_len;
} fl_net_tcp_fsm_conn_t;

typedef struct {
    unsigned in_use;
    uint16_t port_host;
} fl_net_tcp_fsm_listen_t;

static fl_net_tcp_fsm_conn_t s_conn[FL_NET_TCP_FSM_CONN_MAX];
static fl_net_tcp_fsm_listen_t s_listen[FL_NET_TCP_FSM_LISTEN_MAX];
static uint32_t s_isn_base = 0x51510000u;

static uint16_t tcp_read_port_be(const uint8_t *tcp) {
    return (uint16_t)(((uint16_t)tcp[0] << 8) | (uint16_t)tcp[1]);
}

static uint32_t tcp_read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void tcp_write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}


static size_t tcp_build_rst_ack_seg(const uint8_t *syn, size_t syn_len, uint8_t *reply, size_t cap) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_tcp_build_rst_ack(syn, syn_len, reply, cap);
#else
    uint32_t seq;

    if (!syn || syn_len < FL_NET_TCP_HDR_LEN_MIN || !reply || cap < FL_NET_TCP_HDR_LEN_MIN)
        return 0;
    if (!(syn[13] & FL_NET_TCP_FLAG_SYN))
        return 0;
    memset(reply, 0, FL_NET_TCP_HDR_LEN_MIN);
    seq = tcp_read_u32_be(syn + 4) + 1u;
    reply[0] = syn[2];
    reply[1] = syn[3];
    reply[2] = syn[0];
    reply[3] = syn[1];
    tcp_write_u32_be(reply + 4, 0);
    tcp_write_u32_be(reply + 8, seq);
    reply[12] = (uint8_t)(5u << 4);
    reply[13] = (uint8_t)(FL_NET_TCP_FLAG_RST | FL_NET_TCP_FLAG_ACK);
    return FL_NET_TCP_HDR_LEN_MIN;
#endif
}

static size_t tcp_build_segment(uint8_t *buf, size_t cap, uint16_t sport, uint16_t dport,
                                uint32_t seq_be, uint32_t ack_be, uint8_t flags,
                                const uint8_t *payload, size_t payload_len, uint32_t src_be,
                                uint32_t dst_be) {
    size_t hdr_len = FL_NET_TCP_HDR_LEN_MIN;
    size_t total;
    uint16_t csum;

    if (!buf || cap < hdr_len + payload_len)
        return 0;
    total = hdr_len + payload_len;
    memset(buf, 0, hdr_len);
    buf[0] = (uint8_t)(sport >> 8);
    buf[1] = (uint8_t)(sport & 0xff);
    buf[2] = (uint8_t)(dport >> 8);
    buf[3] = (uint8_t)(dport & 0xff);
    tcp_write_u32_be(buf + 4, seq_be);
    tcp_write_u32_be(buf + 8, ack_be);
    buf[12] = (uint8_t)(5u << 4);
    buf[13] = flags;
    buf[14] = 0x20;
    buf[15] = 0x00;
    if (payload_len > 0u && payload)
        memcpy(buf + hdr_len, payload, payload_len);
    csum = fl_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_TCP, buf, total);
    buf[16] = (uint8_t)(csum >> 8);
    buf[17] = (uint8_t)(csum & 0xff);
    return total;
}

static fl_net_tcp_fsm_conn_t *conn_alloc(void) {
    for (unsigned i = 0; i < FL_NET_TCP_FSM_CONN_MAX; i++) {
        if (!s_conn[i].in_use) {
            memset(&s_conn[i], 0, sizeof(s_conn[i]));
            s_conn[i].in_use = 1u;
            return &s_conn[i];
        }
    }
    return NULL;
}

static fl_net_tcp_fsm_conn_t *conn_find(unsigned conn_id) {
    if (conn_id >= FL_NET_TCP_FSM_CONN_MAX || !s_conn[conn_id].in_use)
        return NULL;
    return &s_conn[conn_id];
}

static fl_net_tcp_fsm_listen_t *listen_find(uint16_t port) {
    for (unsigned i = 0; i < FL_NET_TCP_FSM_LISTEN_MAX; i++) {
        if (s_listen[i].in_use && s_listen[i].port_host == port)
            return &s_listen[i];
    }
    return NULL;
}

static fl_net_tcp_fsm_conn_t *conn_find_quad(uint32_t local_ip, uint16_t local_port,
                                             uint32_t remote_ip, uint16_t remote_port) {
    for (unsigned i = 0; i < FL_NET_TCP_FSM_CONN_MAX; i++) {
        fl_net_tcp_fsm_conn_t *c = &s_conn[i];
        if (!c->in_use)
            continue;
        if (c->local_ip_be == local_ip && c->local_port == local_port &&
            c->remote_ip_be == remote_ip && c->remote_port == remote_port)
            return c;
    }
    return NULL;
}


static void conn_release(fl_net_tcp_fsm_conn_t *c) {
    if (c)
        memset(c, 0, sizeof(*c));
}

static int tcp_local_port_in_use(uint16_t sport_host) {
    for (unsigned i = 0; i < FL_NET_TCP_FSM_CONN_MAX; i++) {
        if (s_conn[i].in_use && s_conn[i].local_port == sport_host)
            return 1;
    }
    return 0;
}

static uint16_t tcp_ephemeral_port_pick(void) {
    static uint32_t s_next = FL_NET_UDP_EPHEMERAL_PORT_MIN;

    for (unsigned t = 0; t < 4096u; t++) {
        uint16_t p = (uint16_t)(s_next & 0xffffu);

        s_next++;
        if (p < (uint16_t)FL_NET_UDP_EPHEMERAL_PORT_MIN ||
            p > (uint16_t)FL_NET_UDP_EPHEMERAL_PORT_MAX) {
            s_next = FL_NET_UDP_EPHEMERAL_PORT_MIN;
            p = (uint16_t)FL_NET_UDP_EPHEMERAL_PORT_MIN;
        }
        if (!tcp_local_port_in_use(p))
            return p;
    }
    return 0;
}

static fl_result_t tcp_resolve_src(uint32_t dst_be, uint32_t *src_be_out) {
    if (!src_be_out)
        return FL_RESULT_INVAL;
    if (fl_net_ipv4_is_loopback(dst_be)) {
        *src_be_out = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
        return FL_RESULT_OK;
    }
    {
        fl_net_route_entry_t route;

        if (fl_net_route_lookup(dst_be, &route) != FL_RESULT_OK)
            return FL_RESULT_NOENT;
        *src_be_out = route.src_ip_be;
    }
    return FL_RESULT_OK;
}

void fl_net_tcp_fsm_reset(void) {
    memset(s_conn, 0, sizeof(s_conn));
    memset(s_listen, 0, sizeof(s_listen));
    s_isn_base += 0x1000u;
}

fl_result_t fl_net_tcp_listen_port(uint16_t port_host) {
    if (port_host == 0u)
        return FL_RESULT_INVAL;
    if (listen_find(port_host))
        return FL_RESULT_OK;
    for (unsigned i = 0; i < FL_NET_TCP_FSM_LISTEN_MAX; i++) {
        if (!s_listen[i].in_use) {
            s_listen[i].in_use = 1u;
            s_listen[i].port_host = port_host;
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_BUSY;
}

fl_result_t fl_net_tcp_connect(uint32_t dst_be, uint16_t dport_host, unsigned *conn_id_out) {
    uint8_t syn[FL_NET_TCP_HDR_LEN_MIN];
    uint8_t rx[FL_NET_TCP_HDR_LEN_MIN + 16];
    size_t rx_len = 0;
    uint32_t src_be;
    uint16_t sport;
    fl_net_tcp_fsm_conn_t *c;
    fl_net_packet_t pkt;
    fl_result_t rc;
    unsigned conn_id;

    if (!conn_id_out || dport_host == 0u)
        return FL_RESULT_INVAL;

    rc = tcp_resolve_src(dst_be, &src_be);
    if (rc != FL_RESULT_OK)
        return rc;

    sport = tcp_ephemeral_port_pick();
    if (sport == 0u)
        return FL_RESULT_BUSY;

    c = conn_alloc();
    if (!c)
        return FL_RESULT_BUSY;

    conn_id = (unsigned)(c - s_conn);
    c->state = FL_NET_TCP_STATE_SYN_SENT;
    c->local_ip_be = src_be;
    c->remote_ip_be = dst_be;
    c->local_port = sport;
    c->remote_port = dport_host;
    c->iss = s_isn_base + (uint32_t)conn_id;
    c->snd_nxt = c->iss + 1u;
    c->rcv_nxt = 0;

    if (tcp_build_segment(syn, sizeof(syn), sport, dport_host, c->iss, 0, FL_NET_TCP_FLAG_SYN, NULL,
                          0, src_be, dst_be) != FL_NET_TCP_HDR_LEN_MIN) {
        conn_release(c);
        return FL_RESULT_ERR;
    }

    rc = fl_net_packet_bind_l4(&pkt, syn, sizeof(syn), 0u, FL_NET_TCP_HDR_LEN_MIN);
    if (rc != FL_RESULT_OK) {
        conn_release(c);
        return rc;
    }

    rc = fl_net_wire_egress_l4_pkt(dst_be, FL_NET_IP_PROTO_TCP, &pkt, rx, sizeof(rx), &rx_len, 3000u,
                                   NULL);
    if (rc != FL_RESULT_OK) {
        conn_release(c);
        return rc;
    }
    if (rx_len < FL_NET_TCP_HDR_LEN_MIN) {
        conn_release(c);
        return FL_RESULT_ERR;
    }

    {
        const uint8_t *rx_tcp = rx;
        uint8_t flags = rx_tcp[13];
        uint32_t ack_seq = tcp_read_u32_be(rx_tcp + 8);

        if (flags & FL_NET_TCP_FLAG_RST) {
            conn_release(c);
            return FL_RESULT_EOF;
        }
        if ((flags & (FL_NET_TCP_FLAG_SYN | FL_NET_TCP_FLAG_ACK)) !=
                (FL_NET_TCP_FLAG_SYN | FL_NET_TCP_FLAG_ACK)) {
            conn_release(c);
            return FL_RESULT_ERR;
        }
        if (ack_seq != c->iss + 1u) {
            conn_release(c);
            return FL_RESULT_ERR;
        }
        c->rcv_nxt = tcp_read_u32_be(rx_tcp + 4) + 1u;
    }

    {
        uint8_t ack_seg[FL_NET_TCP_HDR_LEN_MIN];
        size_t ack_len = tcp_build_segment(ack_seg, sizeof(ack_seg), sport, dport_host, c->snd_nxt,
                                           c->rcv_nxt, FL_NET_TCP_FLAG_ACK, NULL, 0, src_be,
                                           dst_be);
        fl_net_packet_t ack_pkt;

        if (ack_len != FL_NET_TCP_HDR_LEN_MIN) {
            conn_release(c);
            return FL_RESULT_ERR;
        }
        rc = fl_net_packet_bind_l4(&ack_pkt, ack_seg, sizeof(ack_seg), 0u, ack_len);
        if (rc != FL_RESULT_OK) {
            conn_release(c);
            return rc;
        }
        rc = fl_net_wire_egress_l4_xmit_pkt(dst_be, FL_NET_IP_PROTO_TCP, &ack_pkt, 500u);
        if (rc != FL_RESULT_OK) {
            conn_release(c);
            return rc;
        }
    }

    c->state = FL_NET_TCP_STATE_ESTABLISHED;
    *conn_id_out = conn_id;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tcp_accept(uint16_t listen_port_host, unsigned *conn_id_out) {
    if (!conn_id_out)
        return FL_RESULT_INVAL;
    for (unsigned i = 0; i < FL_NET_TCP_FSM_CONN_MAX; i++) {
        fl_net_tcp_fsm_conn_t *c = &s_conn[i];
        if (c->in_use && c->accept_ready && c->local_port == listen_port_host &&
            c->state == FL_NET_TCP_STATE_ESTABLISHED) {
            c->accept_ready = 0u;
            *conn_id_out = i;
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_TIMEDOUT;
}

fl_result_t fl_net_tcp_send(unsigned conn_id, const uint8_t *data, size_t len) {
    fl_net_tcp_fsm_conn_t *c = conn_find(conn_id);
    uint8_t seg[FL_NET_TCP_HDR_LEN_MIN + FL_NET_TCP_FSM_RX_CAP];
    size_t seg_len;
    fl_net_packet_t pkt;
    fl_result_t rc;

    if (!c || c->state != FL_NET_TCP_STATE_ESTABLISHED)
        return FL_RESULT_INVAL;
    if (!data && len > 0u)
        return FL_RESULT_INVAL;
    if (len > FL_NET_TCP_FSM_RX_CAP)
        return FL_RESULT_ERR;

    seg_len = tcp_build_segment(seg, sizeof(seg), c->local_port, c->remote_port, c->snd_nxt,
                                c->rcv_nxt, (uint8_t)(FL_NET_TCP_FLAG_ACK | FL_NET_TCP_FLAG_PSH),
                                data, len, c->local_ip_be, c->remote_ip_be);
    if (seg_len == 0)
        return FL_RESULT_ERR;

    rc = fl_net_packet_bind_l4(&pkt, seg, sizeof(seg), 0u, seg_len);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_net_wire_egress_l4_xmit_pkt(c->remote_ip_be, FL_NET_IP_PROTO_TCP, &pkt, 500u);
    if (rc != FL_RESULT_OK)
        return rc;
    c->snd_nxt += (uint32_t)len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tcp_recv(unsigned conn_id, uint8_t *buf, size_t cap, size_t *out_len) {
    fl_net_tcp_fsm_conn_t *c = conn_find(conn_id);

    if (!buf || !out_len)
        return FL_RESULT_INVAL;
    if (!c || c->state != FL_NET_TCP_STATE_ESTABLISHED)
        return FL_RESULT_INVAL;
    if (c->rx_len == 0)
        return FL_RESULT_TIMEDOUT;
    if (cap < c->rx_len)
        return FL_RESULT_ERR;
    memcpy(buf, c->rx_buf, c->rx_len);
    *out_len = c->rx_len;
    c->rx_len = 0;
    return FL_RESULT_OK;
}

fl_result_t fl_net_tcp_close(unsigned conn_id) {
    fl_net_tcp_fsm_conn_t *c = conn_find(conn_id);

    if (!c)
        return FL_RESULT_INVAL;
    conn_release(c);
    return FL_RESULT_OK;
}

fl_result_t fl_net_tcp_loopback_input(uint32_t src_be, uint32_t dst_be, const uint8_t *tcp,
                                      size_t tcp_len, uint8_t *reply, size_t reply_cap,
                                      size_t *reply_len) {
    uint16_t sport;
    uint16_t dport;
    uint8_t flags;
    uint32_t seq;
    uint32_t ack;
    size_t hdr_len;
    size_t plen;
    const uint8_t *payload;

    if (!tcp || tcp_len < FL_NET_TCP_HDR_LEN_MIN || !reply || !reply_len)
        return FL_RESULT_INVAL;

    *reply_len = 0;
    sport = tcp_read_port_be(tcp);
    dport = tcp_read_port_be(tcp + 2);
    seq = tcp_read_u32_be(tcp + 4);
    ack = tcp_read_u32_be(tcp + 8);
    flags = tcp[13];
    hdr_len = (size_t)((tcp[12] >> 4) * 4u);
    if (hdr_len < FL_NET_TCP_HDR_LEN_MIN || hdr_len > tcp_len)
        return FL_RESULT_ERR;
    payload = tcp + hdr_len;
    plen = tcp_len - hdr_len;

    if ((flags & FL_NET_TCP_FLAG_SYN) && !(flags & FL_NET_TCP_FLAG_ACK)) {
        fl_net_tcp_fsm_conn_t *c;

        if (!listen_find(dport)) {
            size_t rst_len = tcp_build_rst_ack_seg(tcp, tcp_len, reply, reply_cap);
            if (rst_len == 0)
                return FL_RESULT_TIMEDOUT;
            *reply_len = rst_len;
            return FL_RESULT_OK;
        }

        for (unsigned i = 0; i < FL_NET_TCP_FSM_CONN_MAX; i++) {
            fl_net_tcp_fsm_conn_t *existing = &s_conn[i];

            if (existing->state != FL_NET_TCP_STATE_SYN_RECV)
                continue;
            if (existing->local_ip_be != dst_be || existing->remote_ip_be != src_be ||
                existing->local_port != dport || existing->remote_port != sport)
                continue;
            c = existing;
            c->rcv_nxt = seq + 1u;
            goto syn_reply;
        }

        c = conn_alloc();
        if (!c)
            return FL_RESULT_ERR;

        c->state = FL_NET_TCP_STATE_SYN_RECV;
        c->local_ip_be = dst_be;
        c->remote_ip_be = src_be;
        c->local_port = dport;
        c->remote_port = sport;
        c->iss = s_isn_base + 0x8000u + (uint32_t)(c - s_conn);
        c->rcv_nxt = seq + 1u;
        c->snd_nxt = c->iss + 1u;
    syn_reply:

        *reply_len = tcp_build_segment(reply, reply_cap, dport, sport, c->iss, c->rcv_nxt,
                                       (uint8_t)(FL_NET_TCP_FLAG_SYN | FL_NET_TCP_FLAG_ACK), NULL,
                                       0, dst_be, src_be);
        return (*reply_len > 0) ? FL_RESULT_OK : FL_RESULT_ERR;
    }

    {
        fl_net_tcp_fsm_conn_t *c =
            conn_find_quad(dst_be, dport, src_be, sport);

        if (!c)
            return FL_RESULT_TIMEDOUT;

        if ((flags & FL_NET_TCP_FLAG_ACK) && c->state == FL_NET_TCP_STATE_SYN_RECV) {
            if (ack != c->snd_nxt || seq != c->rcv_nxt)
                return FL_RESULT_ERR;
            c->state = FL_NET_TCP_STATE_ESTABLISHED;
            c->accept_ready = 1u;
            return FL_RESULT_OK;
        }

        if ((flags & FL_NET_TCP_FLAG_ACK) && plen > 0 &&
            c->state == FL_NET_TCP_STATE_ESTABLISHED) {
            if (seq != c->rcv_nxt)
                return FL_RESULT_ERR;
            if (c->rx_len + plen > FL_NET_TCP_FSM_RX_CAP)
                return FL_RESULT_ERR;
            memcpy(c->rx_buf + c->rx_len, payload, plen);
            c->rx_len += plen;
            c->rcv_nxt += (uint32_t)plen;
            *reply_len = tcp_build_segment(reply, reply_cap, dport, sport, c->snd_nxt, c->rcv_nxt,
                                           FL_NET_TCP_FLAG_ACK, NULL, 0, dst_be, src_be);
            return (*reply_len > 0) ? FL_RESULT_OK : FL_RESULT_ERR;
        }
    }

    (void)ack;
    return FL_RESULT_TIMEDOUT;
}
