#ifndef NET_ROUTE_H
#define NET_ROUTE_H

#include "contract_result.h"
#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

#ifndef FL_NET_ROUTE_TABLE_MAX
#define FL_NET_ROUTE_TABLE_MAX 8u
#endif

typedef struct {
    uint32_t addr_be;
    uint8_t prefix_len;
    uint32_t gw_be;
    fl_net_driver_t *drv;
    uint32_t src_ip_be;
    uint8_t src_mac[6];
    int src_mac_valid;
} fl_net_route_entry_t;

void fl_net_route_init(void);
void fl_net_route_clear(void);

fl_result_t fl_net_route_add(uint32_t addr_be, uint8_t prefix_len, uint32_t gw_be,
                             fl_net_driver_t *drv, uint32_t src_ip_be,
                             const uint8_t src_mac[6]);

/**
 * Longest-prefix match. On success, **out** holds the route; use **fl_net_route_next_hop**
 * for on-link **dst_be** or the gateway when off-subnet.
 */
fl_result_t fl_net_route_lookup(uint32_t dst_be, fl_net_route_entry_t *out);

/** On-link **dst** or **gw** when the matched route is off-subnet. */
uint32_t fl_net_route_next_hop(uint32_t dst_be, const fl_net_route_entry_t *route);

/** Register loopback **127.0.0.0/8** route (idempotent). */
void fl_net_route_add_loopback(void);

/**
 * Register a static on-subnet route (bare-metal lab or hosted TAP).
 * Rejects non-zero prefix-length-0 nets (only 0.0.0.0 at prefix 0 is valid).
 * Off-subnet traffic uses the route **gw** or hosted socket fallbacks until **P3-12**
 * DHCP / production default-route policy lands.
 */
fl_result_t fl_net_route_configure_static(fl_net_driver_t *drv, const uint8_t src_mac[6],
                                          const char *addr_s, unsigned prefix_len,
                                          const char *gw_s);

#if defined(__linux__)
/** Apply **FL_NET_TAP_IPV4**, **FL_NET_TAP_PREFIX**, **FL_NET_TAP_GW** when TAP is open. */
fl_result_t fl_net_route_configure_tap(fl_net_driver_t *tap_drv, const uint8_t tap_mac[6],
                                       const char *tap_ifname);
#endif

int fl_net_ipv4_prefix_match(uint32_t addr_be, uint32_t net_be, uint8_t prefix_len);

#endif /* NET_ROUTE_H */
