/**
 * **P3-12 — DHCP client (IPv4)** (module contract, normative).
 *
 * **Distribution:** client packets are **UDP** payloads (**P3-6**) carrying BOOTP/DHCP
 * messages; options blocks begin after the fixed header and must start with magic
 * **FL_NET_DHCP_MAGIC_COOKIE_BE32** (**RFC 2131** / **RFC 2132** subset documented in code).
 * Layered offsets use **fl_net_packet_t** / **fl_net_pkt_slice_t** (**contract_p3_packet.h**);
 * lab client binds BOOTP with **fl_net_packet_bind_l4** in **net_dhcp.c** / **net_packet.c**.
 *
 * **State machine:** **DISCOVER → OFFER → REQUEST → ACK** with finite timers per state;
 * document **NAK** and lease expiry paths.
 */
#ifndef FL_CONTRACT_P3_DHCP_H
#define FL_CONTRACT_P3_DHCP_H

#include "contract_p3_wire.h"

#define FL_CONTRACT_P3_12_DHCP_CONTRACT_DEFINED 1

typedef enum {
    FL_NET_DHCP_MSG_DISCOVER = 1,
    FL_NET_DHCP_MSG_OFFER = 2,
    FL_NET_DHCP_MSG_REQUEST = 3,
    FL_NET_DHCP_MSG_DECLINE = 4,
    FL_NET_DHCP_MSG_ACK = 5,
    FL_NET_DHCP_MSG_NAK = 6,
    FL_NET_DHCP_MSG_RELEASE = 7
} fl_net_dhcp_message_type_t;

/** BOOTP fixed header length before options (**RFC 951** / **RFC 2131**). */
#define FL_NET_BOOTP_FIXED_LEN 236u

#define FL_NET_DHCP_SERVER_PORT 67u
#define FL_NET_DHCP_CLIENT_PORT 68u

/** Option codes used by the lab client subset. */
#define FL_NET_DHCP_OPT_PAD 0u
#define FL_NET_DHCP_OPT_MESSAGE_TYPE 53u
#define FL_NET_DHCP_OPT_END 255u

#endif /* FL_CONTRACT_P3_DHCP_H */
