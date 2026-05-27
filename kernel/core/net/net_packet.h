#ifndef NET_PACKET_H
#define NET_PACKET_H

/**
 * **P3 packet layering** — **contract_p3_packet.h** implementation.
 * Layered slices and RX/TX pipeline helpers over **net_wire** frame vocabulary.
 */
#include "contract_p3_packet.h"
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_NET_PACKET_IMPL_DEFINED 1

void fl_net_packet_reset(fl_net_packet_t *pkt);
void fl_net_pipeline_rx_reset(fl_net_pipeline_rx_t *pipe);
void fl_net_pipeline_tx_reset(fl_net_pipeline_tx_t *pipe);

/**
 * Parse **DIX Ethernet + IPv4** from **frame** into **out** (sets **valid** bits).
 * ARP and non-IPv4 ethertypes return **FL_RESULT_ERR**.
 */
fl_result_t fl_net_packet_parse_eth_ipv4(const uint8_t *frame, size_t len, fl_net_packet_t *out);

/** Copy L4 payload bytes when **FL_NET_PKT_VALID_L4** is set. */
fl_result_t fl_net_packet_copy_l4(const fl_net_packet_t *pkt, uint8_t *dst, size_t cap,
                                  size_t *out_len);

/**
 * Bind **backing** as an L4-only packet (UDP/TCP/DHCP BOOTP payload without Ethernet parse).
 * Used by **P3-6** / **P3-12** before **fl_net_udp_build_datagram** or **fl_net_packet_copy_l4**.
 */
fl_result_t fl_net_packet_bind_l4(fl_net_packet_t *pkt, uint8_t *backing, size_t backing_len,
                                  size_t l4_off, size_t l4_len);

/** Non-owning L4 slice pointer when **FL_NET_PKT_VALID_L4** is set. */
fl_result_t fl_net_packet_l4_view(const fl_net_packet_t *pkt, const uint8_t **data_out,
                                  size_t *len_out);

/** Advance RX pipeline to **stage** after filling **pipe->pkt** from **frame**. */
fl_result_t fl_net_pipeline_rx_feed(fl_net_pipeline_rx_t *pipe, fl_net_pipeline_stage_t stage,
                                    const uint8_t *frame, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NET_PACKET_H */
