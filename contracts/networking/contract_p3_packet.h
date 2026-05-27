/**
 * **P3 packet layering** — cross-cutting interchange model (not a separate **P3-*** row).
 *
 * **Distribution:** every **P3-4** … **P3-12** path moves octets as **fl_net_frame_view_t**
 * / **fl_net_frame_mut_t** (**contract_p3_wire.h**). This shard names **layer slices**
 * (L2 / IPv4 / L4) and **RX/TX pipeline stages** so ARP, routing, UDP, DHCP, and future
 * socket code share one parse/build vocabulary without duplicating offset math.
 *
 * **Implementation:** **kernel/core/net/net_packet.c** (**net_packet.h**). **P3-6** UDP,
 * **P3-12** DHCP, **net_wire_egress**, and **net_wire_host** move app/L4 bytes via
 * **fl_net_packet_bind_l4**, **fl_net_packet_l4_view**, and **fl_net_packet_copy_l4**.
 */
#ifndef FL_CONTRACT_P3_PACKET_H
#define FL_CONTRACT_P3_PACKET_H

#include "contract_p3_ipv4.h"
#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_PACKET_CONTRACT_DEFINED 1

/** Bit in **fl_net_packet_t.valid** after a layer is parsed. */
#define FL_NET_PKT_VALID_L2 ((unsigned)1u << 0)
#define FL_NET_PKT_VALID_IPV4 ((unsigned)1u << 1)
#define FL_NET_PKT_VALID_L4 ((unsigned)1u << 2)

/** Non-owning byte range inside a parent frame buffer. */
typedef struct {
    size_t off;
    size_t len;
} fl_net_pkt_slice_t;

/**
 * Parsed view of one Ethernet+IPv4 frame (optional L4 payload slice).
 * **frame** must outlive all slice reads; callers own the backing storage.
 */
typedef struct {
    fl_net_frame_view_t frame;
    fl_net_pkt_slice_t l2;
    fl_net_pkt_slice_t ipv4;
    fl_net_pkt_slice_t l4;
    uint16_t ethertype_be16;
    fl_ipv4_be32_t src_be;
    fl_ipv4_be32_t dst_be;
    uint8_t ip_proto;
    unsigned valid;
} fl_net_packet_t;

/** RX path stages (hosted netdev and loopback today; bare-metal adds **P3-1** hooks). */
typedef enum {
    FL_NET_PIPE_STAGE_NONE = 0,
    FL_NET_PIPE_STAGE_DRV_RX,
    FL_NET_PIPE_STAGE_PARSE_L2,
    FL_NET_PIPE_STAGE_PARSE_L3,
    FL_NET_PIPE_STAGE_PARSE_L4,
    FL_NET_PIPE_STAGE_ROUTE,
    FL_NET_PIPE_STAGE_DELIVER,
} fl_net_pipeline_stage_t;

/** TX path stages (build → route → ARP → netdev). */
typedef enum {
    FL_NET_PIPE_TX_STAGE_NONE = 0,
    FL_NET_PIPE_TX_STAGE_BUILD_L4,
    FL_NET_PIPE_TX_STAGE_BUILD_IPV4,
    FL_NET_PIPE_TX_STAGE_BUILD_L2,
    FL_NET_PIPE_TX_STAGE_ROUTE,
    FL_NET_PIPE_TX_STAGE_ARP,
    FL_NET_PIPE_TX_STAGE_DRV_TX,
} fl_net_pipeline_tx_stage_t;

/** One RX pipeline step: stage + parsed packet + last outcome. */
typedef struct {
    fl_net_pipeline_stage_t stage;
    fl_net_packet_t pkt;
    fl_result_t last_rc;
} fl_net_pipeline_rx_t;

/** One TX pipeline step: stage + frame view under construction. */
typedef struct {
    fl_net_pipeline_tx_stage_t stage;
    fl_net_frame_view_t frame;
    fl_result_t last_rc;
} fl_net_pipeline_tx_t;

#endif /* FL_CONTRACT_P3_PACKET_H */
