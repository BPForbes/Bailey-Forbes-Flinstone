#ifndef NET_DHCP_H
#define NET_DHCP_H

#include "contract_p3_dhcp.h"
#include "contract_p3_packet.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * Build a BOOTREQUEST with DHCP message type option (discover or request).
 * **mac** is six octets; **xid** is stored big-endian on the wire.
 */
fl_result_t fl_net_dhcp_build_request(uint8_t *buf, size_t cap, uint8_t dhcp_msg_type, uint32_t xid,
                                      const uint8_t mac[FL_NET_ETH_ADDR_LEN], size_t *out_len);

/**
 * Build a BOOTREQUEST and bind **pkt** L4 slice over **backing** (**contract_p3_packet.h**).
 */
fl_result_t fl_net_dhcp_build_request_pkt(fl_net_packet_t *pkt, uint8_t *backing, size_t cap,
                                          uint8_t dhcp_msg_type, uint32_t xid,
                                          const uint8_t mac[FL_NET_ETH_ADDR_LEN]);

/**
 * Parse a BOOTREPLY: **yiaddr**, DHCP message type option, and **xid**.
 */
fl_result_t fl_net_dhcp_parse_reply(const uint8_t *buf, size_t len, uint32_t *xid_out,
                                    uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out);

/** Parse BOOTREPLY from **pkt** L4 slice (**fl_net_packet_bind_l4** or RX parse). */
fl_result_t fl_net_dhcp_parse_reply_pkt(const fl_net_packet_t *pkt, uint32_t *xid_out,
                                      uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out);

/**
 * Lab client: DISCOVER then REQUEST after OFFER (hosted UDP). On success optionally installs
 * a /24 route via **fl_net_route_add** when **tap_drv** and **tap_mac** are provided.
 */
fl_result_t fl_net_dhcp_lab_acquire(uint32_t *leased_addr_be, unsigned timeout_ms);

#endif /* NET_DHCP_H */
