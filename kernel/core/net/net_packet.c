#include "net_packet.h"

#include "net_wire.h"

#include <string.h>

void fl_net_packet_reset(fl_net_packet_t *pkt) {
    if (!pkt)
        return;
    memset(pkt, 0, sizeof(*pkt));
}

void fl_net_pipeline_rx_reset(fl_net_pipeline_rx_t *pipe) {
    if (!pipe)
        return;
    pipe->stage = FL_NET_PIPE_STAGE_NONE;
    pipe->last_rc = FL_RESULT_OK;
    fl_net_packet_reset(&pipe->pkt);
}

void fl_net_pipeline_tx_reset(fl_net_pipeline_tx_t *pipe) {
    if (!pipe)
        return;
    pipe->stage = FL_NET_PIPE_TX_STAGE_NONE;
    pipe->frame.data = NULL;
    pipe->frame.len = 0;
    pipe->last_rc = FL_RESULT_OK;
}

fl_result_t fl_net_packet_parse_eth_ipv4(const uint8_t *frame, size_t len, fl_net_packet_t *out) {
    size_t ip_off;
    size_t ip_len;
    fl_ipv4_be32_t dst_be;
    const uint8_t *ip;
    size_t hdr_len;
    int ok;

    if (!frame || !out)
        return FL_RESULT_INVAL;

    fl_net_packet_reset(out);
    out->frame = fl_net_frame_view_make(frame, len);

    if (len < FL_NET_ETH_FRAME_HDR_LEN)
        return FL_RESULT_ERR;

    out->l2.off = 0;
    out->l2.len = (size_t)FL_NET_ETH_FRAME_HDR_LEN;
    out->valid |= FL_NET_PKT_VALID_L2;

    out->ethertype_be16 = fl_net_wire_ethertype_be16(frame, len, &ok);
    if (!ok || out->ethertype_be16 != FL_ETHERTYPE_IPV4)
        return FL_RESULT_ERR;

    if (!fl_net_wire_parse_eth_ipv4(frame, len, &ip_off, &ip_len, &dst_be))
        return FL_RESULT_ERR;

    ip = frame + ip_off;
    hdr_len = (size_t)((ip[0] & 0x0fu) * 4u);
    if (hdr_len < FL_NET_IPV4_HDR_LEN_MIN || ip_len < hdr_len || ip_off + ip_len > len)
        return FL_RESULT_ERR;

    out->ipv4.off = ip_off;
    out->ipv4.len = ip_len;
    out->valid |= FL_NET_PKT_VALID_IPV4;

    out->dst_be = dst_be;
    out->src_be = (fl_ipv4_be32_t)((uint32_t)ip[12] | ((uint32_t)ip[13] << 8) |
                                   ((uint32_t)ip[14] << 16) | ((uint32_t)ip[15] << 24));
    out->ip_proto = ip[9];

    if (ip_len > hdr_len) {
        out->l4.off = ip_off + hdr_len;
        out->l4.len = ip_len - hdr_len;
        out->valid |= FL_NET_PKT_VALID_L4;
    }

    return FL_RESULT_OK;
}

fl_result_t fl_net_packet_copy_l4(const fl_net_packet_t *pkt, uint8_t *dst, size_t cap,
                                  size_t *out_len) {
    const uint8_t *base;

    if (!pkt || !dst || !out_len)
        return FL_RESULT_INVAL;
    if ((pkt->valid & FL_NET_PKT_VALID_L4) == 0)
        return FL_RESULT_ERR;
    if (pkt->l4.len > cap)
        return FL_RESULT_ERR;

    base = pkt->frame.data;
    if (!base || pkt->l4.off + pkt->l4.len > pkt->frame.len)
        return FL_RESULT_ERR;

    memcpy(dst, base + pkt->l4.off, pkt->l4.len);
    *out_len = pkt->l4.len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_pipeline_rx_feed(fl_net_pipeline_rx_t *pipe, fl_net_pipeline_stage_t stage,
                                    const uint8_t *frame, size_t len) {
    fl_result_t rc;

    if (!pipe)
        return FL_RESULT_INVAL;

    pipe->stage = stage;
    if (stage == FL_NET_PIPE_STAGE_NONE)
        return FL_RESULT_OK;

    if (stage < FL_NET_PIPE_STAGE_PARSE_L2) {
        pipe->pkt.frame = fl_net_frame_view_make(frame, len);
        pipe->last_rc = FL_RESULT_OK;
        return FL_RESULT_OK;
    }

    rc = fl_net_packet_parse_eth_ipv4(frame, len, &pipe->pkt);
    pipe->last_rc = rc;
    if (rc != FL_RESULT_OK)
        return rc;

    if (stage == FL_NET_PIPE_STAGE_PARSE_L2)
        return FL_RESULT_OK;
    if (stage == FL_NET_PIPE_STAGE_PARSE_L3 &&
        (pipe->pkt.valid & FL_NET_PKT_VALID_IPV4) != 0)
        return FL_RESULT_OK;
    if (stage >= FL_NET_PIPE_STAGE_PARSE_L4 &&
        (pipe->pkt.valid & FL_NET_PKT_VALID_L4) != 0)
        return FL_RESULT_OK;
    if (stage >= FL_NET_PIPE_STAGE_PARSE_L4)
        return FL_RESULT_ERR;

    return FL_RESULT_OK;
}
