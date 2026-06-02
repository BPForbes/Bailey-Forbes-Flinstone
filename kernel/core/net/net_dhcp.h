#ifndef NET_DHCP_H
#define NET_DHCP_H

#include "contract_p3_dhcp.h"
#include "contract_p3_packet.h"
#include "contract_result.h"
#include "fl/driver/net.h"

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

typedef struct {
    uint32_t yiaddr_be;
    uint32_t subnet_mask_be;
    uint32_t router_be;
    unsigned prefix_len;
    uint8_t msg_type;
} fl_net_dhcp_lease_t;

/**
 * Parse a BOOTREPLY: **yiaddr**, DHCP message type option, and **xid**.
 */
fl_result_t fl_net_dhcp_parse_reply(const uint8_t *buf, size_t len, uint32_t *xid_out,
                                    uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out);

/** Parse BOOTREPLY from **pkt** L4 slice (**fl_net_packet_bind_l4** or RX parse). */
fl_result_t fl_net_dhcp_parse_reply_pkt(const fl_net_packet_t *pkt, uint32_t *xid_out,
                                        uint32_t *yiaddr_be_out, uint8_t *dhcp_msg_type_out);

/**
 * Parse DHCP options (subnet mask, default router) after a successful OFFER/ACK.
 * Missing options leave **lease** fields zero; **prefix_len** defaults to **24** when
 * **yiaddr_be** is set but mask is absent.
 */
fl_result_t fl_net_dhcp_parse_lease(const uint8_t *buf, size_t len, fl_net_dhcp_lease_t *lease);

/**
 * Lab client: DISCOVER then REQUEST after OFFER (hosted UDP). On success optionally installs
 * a /24 route via **fl_net_route_add** when **tap_drv** and **tap_mac** are provided.
 */
fl_result_t fl_net_dhcp_acquire(fl_net_driver_t *drv, const uint8_t mac[FL_NET_ETH_ADDR_LEN],
                                const char *subnet_addr_s, unsigned prefix_len, const char *gw_s,
                                uint32_t *leased_addr_be, unsigned timeout_ms);

/** Same as **fl_net_dhcp_acquire**; when **lease_out** is non-NULL, fills parsed ACK options. */
fl_result_t fl_net_dhcp_acquire_ex(fl_net_driver_t *drv, const uint8_t mac[FL_NET_ETH_ADDR_LEN],
                                   const char *subnet_addr_s, unsigned prefix_len,
                                   const char *gw_s, uint32_t *leased_addr_be,
                                   unsigned timeout_ms, fl_net_dhcp_lease_t *lease_out);

/**
 * Hosted TAP: open TAP (if needed), install a **0.0.0.0/0** pre-DHCP route with **src 0**,
 * run DISCOVER/REQUEST using the TAP hardware MAC, then replace routes with the leased
 * address (options 1 and 3 when present). Requires Linux TUN (`dev/net/tun`) and a
 * host bridge to the LAN (see docs/P3_REAL_NETWORK_PHASE1.md).
 */
fl_result_t fl_net_dhcp_acquire_on_tap(const char *ifname_hint, unsigned timeout_ms,
                                       fl_net_dhcp_lease_t *lease_out);

fl_result_t fl_net_dhcp_lab_acquire(uint32_t *leased_addr_be, unsigned timeout_ms);

#endif /* NET_DHCP_H */
