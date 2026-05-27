#ifndef NET_UDP_H
#define NET_UDP_H

#include "contract_p3_udp.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Build UDP header + **payload** into **buf** (host-order ports).
 * Returns total L4 length (8 + payload_len) or 0 on error.
 */
size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len);

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

/** Non-blocking dequeue for **dport_host**; **FL_RESULT_TIMEDOUT** when empty. */
fl_result_t fl_net_udp_recv_from_port(uint16_t dport_host, fl_net_udp_rx_meta_t *meta,
                                      uint8_t *buf, size_t cap, size_t *out_len);

#endif /* NET_UDP_H */
