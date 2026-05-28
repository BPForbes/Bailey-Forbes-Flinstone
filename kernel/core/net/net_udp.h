#ifndef NET_UDP_H
#define NET_UDP_H

#include "contract_p3_packet.h"
#include "contract_p3_udp.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/** Parsed UDP header (ports in **host** order). */
typedef struct {
    uint16_t sport_host;
    uint16_t dport_host;
    uint16_t length_host;
    const uint8_t *payload;
    size_t payload_len;
} fl_net_udp_parsed_t;

fl_result_t fl_net_udp_parse(const uint8_t *udp, size_t len, uint32_t src_be, uint32_t dst_be,
                             int verify_csum, fl_net_udp_parsed_t *out);

fl_result_t fl_net_udp_xmit_pkt(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                                const fl_net_packet_t *payload_pkt, unsigned arp_timeout_ms);

fl_result_t fl_net_udp_xmit(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                            const uint8_t *payload, size_t payload_len, unsigned arp_timeout_ms);

fl_result_t fl_net_udp_echo_exchange(uint32_t dst_be, uint16_t sport_host, uint16_t dport_host,
                                     const uint8_t *payload, size_t payload_len, uint8_t *rx,
                                     size_t rx_cap, size_t *rx_len, unsigned timeout_ms);

/**
 * Build UDP header + **payload** into **buf** (host-order ports).
 * Returns total L4 length (8 + payload_len) or 0 on error.
 */
size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len);

/** Build UDP L4 from **payload_pkt** app slice (**fl_net_packet_l4_view**). */
size_t fl_net_udp_build_datagram_from_pkt(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                          uint16_t sport_host, uint16_t dport_host,
                                          const fl_net_packet_t *payload_pkt);

/** Metadata returned with a dequeued datagram (**host-order** ports). */
typedef struct {
    uint32_t src_ip_be;
    uint16_t src_port_host;
    uint16_t dst_port_host;
} fl_net_udp_rx_meta_t;

void fl_net_udp_demux_reset(void);

/** Bind **dport_host** (host order). Returns **FL_RESULT_OK** or **FL_RESULT_BUSY**. */
fl_result_t fl_net_udp_bind_port(uint16_t dport_host);

fl_result_t fl_net_udp_unbind_port(uint16_t dport_host);

/**
 * Enqueue **payload** for **dport_host**. Under pressure drops the oldest datagram in
 * that port's queue (**P3-6** drop policy).
 */
fl_result_t fl_net_udp_deliver_inbound(uint32_t src_ip_be, uint16_t src_port_host,
                                       uint16_t dport_host, const uint8_t *payload,
                                       size_t payload_len);

/** Enqueue app payload from **payload_pkt** L4 slice. */
fl_result_t fl_net_udp_deliver_inbound_pkt(uint32_t src_ip_be, uint16_t src_port_host,
                                           uint16_t dport_host,
                                           const fl_net_packet_t *payload_pkt);

/** Non-blocking dequeue for **dport_host**; **FL_RESULT_TIMEDOUT** when empty. */
fl_result_t fl_net_udp_recv_from_port(uint16_t dport_host, fl_net_udp_rx_meta_t *meta,
                                      uint8_t *buf, size_t cap, size_t *out_len);

/** Dequeue into **backing** and bind **pkt_out** L4 over copied bytes. */
fl_result_t fl_net_udp_recv_from_port_pkt(uint16_t dport_host, fl_net_udp_rx_meta_t *meta,
                                          fl_net_packet_t *pkt_out, uint8_t *backing,
                                          size_t backing_cap);

#endif /* NET_UDP_H */
