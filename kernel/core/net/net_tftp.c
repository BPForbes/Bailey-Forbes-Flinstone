#include "net_tftp.h"

#include "contract_p3_loopback.h"
#include "contract_p3_udp.h"
#include "net_packet.h"
#include "net_udp.h"
#include "net_wire_host.h"

#include <string.h>

enum {
    TFTP_OP_RRQ = 1,
    TFTP_OP_DATA = 3,
    TFTP_OP_ACK = 4,
    TFTP_OP_ERR = 5
};

static uint16_t tftp_read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static void tftp_write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

size_t fl_net_tftp_build_rrq(uint8_t *buf, size_t cap, const char *filename, const char *mode) {
    size_t fn;
    size_t md;
    size_t total;

    if (!buf || !filename || !mode)
        return 0;
    fn = strlen(filename);
    md = strlen(mode);
    total = 2u + fn + 1u + md + 1u;
    if (cap < total || fn == 0u || md == 0u)
        return 0;

    tftp_write_be16(buf, TFTP_OP_RRQ);
    memcpy(buf + 2, filename, fn);
    buf[2 + fn] = 0;
    memcpy(buf + 3 + fn, mode, md);
    buf[3 + fn + md] = 0;
    return total;
}

static fl_result_t tftp_xmit_ack(uint32_t server_be, uint16_t sport, uint16_t server_dport,
                                 uint32_t src_be, uint16_t block, uint8_t *tx_l4,
                                 uint8_t *rx_l4, size_t rx_cap, size_t *rx_len,
                                 fl_net_packet_t *rx_pkt, unsigned timeout_ms) {
    fl_net_packet_t ack_pkt;
    size_t ack_l4 = 4u;
    tftp_write_be16(tx_l4 + FL_NET_UDP_HDR_LEN, TFTP_OP_ACK);
    tftp_write_be16(tx_l4 + FL_NET_UDP_HDR_LEN + 2, block);
    if (fl_net_udp_build_datagram(tx_l4, FL_NET_UDP_HDR_LEN + 8, src_be, server_be, sport,
                                  server_dport, tx_l4 + FL_NET_UDP_HDR_LEN, ack_l4) == 0)
        return FL_RESULT_ERR;
    if (fl_net_packet_bind_l4(&ack_pkt, tx_l4, FL_NET_UDP_HDR_LEN + 8, 0u,
                              FL_NET_UDP_HDR_LEN + ack_l4) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    *rx_len = 0;
    return fl_net_wire_send_udp_pkt(server_be, sport, server_dport, &ack_pkt, rx_pkt, rx_l4,
                                    rx_cap, rx_len, timeout_ms);
}

fl_result_t fl_net_tftp_read_file(uint32_t server_be, uint16_t port_host, const char *filename,
                                  uint8_t *buf, size_t cap, size_t *out_len,
                                  unsigned timeout_ms) {
    uint8_t req[FL_NET_UDP_HDR_LEN + 64];
    uint8_t rx_l4[FL_NET_UDP_HDR_LEN + FL_NET_TFTP_BLOCK_SIZE + 16];
    uint8_t tx_l4[FL_NET_UDP_HDR_LEN + 8];
    uint32_t src_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);
    size_t rrq_len;
    size_t rx_len = 0;
    size_t total = 0;
    uint16_t block = 1;
    uint16_t sport = (uint16_t)(FL_NET_UDP_EPHEMERAL_PORT_MIN + 77u);
    uint16_t server_dport = port_host;
    fl_net_packet_t req_pkt;
    fl_net_packet_t rx_pkt;
    fl_result_t rc;

    if (!filename || !buf || !out_len)
        return FL_RESULT_INVAL;

    rrq_len = fl_net_tftp_build_rrq(req + FL_NET_UDP_HDR_LEN,
                                    sizeof(req) - FL_NET_UDP_HDR_LEN, filename, "octet");
    if (rrq_len == 0)
        return FL_RESULT_INVAL;

    if (fl_net_packet_bind_l4(&req_pkt, req, sizeof(req), FL_NET_UDP_HDR_LEN, rrq_len) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;

    rc = fl_net_wire_send_udp_pkt(server_be, sport, port_host, &req_pkt, &rx_pkt, rx_l4,
                                  sizeof(rx_l4), &rx_len, timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    for (;;) {
        fl_net_udp_parsed_t parsed;
        const uint8_t *tftp;
        size_t tftp_len;
        uint16_t op;
        uint16_t blk;

        if (fl_net_udp_parse(rx_l4, rx_len, server_be, src_be, 0, &parsed) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        server_dport = parsed.sport_host;
        tftp = parsed.payload;
        tftp_len = parsed.payload_len;
        if (tftp_len < 4u)
            return FL_RESULT_ERR;

        op = tftp_read_be16(tftp);
        if (op == TFTP_OP_ERR)
            return FL_RESULT_ERR;
        if (op != TFTP_OP_DATA)
            return FL_RESULT_ERR;

        blk = tftp_read_be16(tftp + 2);
        if (blk != block)
            return FL_RESULT_ERR;

        if (tftp_len > 4u) {
            size_t plen = tftp_len - 4u;
            if (total + plen > cap)
                return FL_RESULT_ERR;
            memcpy(buf + total, tftp + 4, plen);
            total += plen;
        }

        if (tftp_len - 4u < FL_NET_TFTP_BLOCK_SIZE) {
            rc = tftp_xmit_ack(server_be, sport, server_dport, src_be, blk, tx_l4, rx_l4,
                               sizeof(rx_l4), &rx_len, &rx_pkt, timeout_ms);
            if (rc != FL_RESULT_OK)
                return rc;
            *out_len = total;
            return FL_RESULT_OK;
        }

        rc = tftp_xmit_ack(server_be, sport, server_dport, src_be, blk, tx_l4, rx_l4,
                           sizeof(rx_l4), &rx_len, &rx_pkt, timeout_ms);
        if (rc != FL_RESULT_OK)
            return rc;

        block++;
    }
}
