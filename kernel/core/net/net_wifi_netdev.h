#ifndef NET_WIFI_NETDEV_H
#define NET_WIFI_NETDEV_H

#include "contract_p3_dhcp.h"
#include "contract_p3_packet.h"
#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "fl/driver/net.h"
#include "net_dhcp.h"

#include <stdint.h>

/**
 * In-tree 802.11 station netdev (Linux wlan0-style binding to fl_net_driver_t).
 * MLME + WPA run in net_wifi_station / net_wifi_mgmt; this module owns L2 TX/RX
 * and Ethernet decap for IPv4 once associated.
 */

fl_net_driver_t *fl_net_wifi_netdev_driver(void);
int fl_net_wifi_netdev_is_up(void);

fl_result_t fl_net_wifi_netdev_up(const fl_net_wifi_scan_entry_t *ap,
                                  const uint8_t sta_mac[6]);

/** Lab default IP (10.0.2.15) or DHCP-leased address on the simulated router LAN. */
fl_result_t fl_net_wifi_netdev_up_with_ipv4(const fl_net_wifi_scan_entry_t *ap,
                                            const uint8_t sta_mac[6],
                                            const char *addr_s, uint8_t prefix_len,
                                            const char *gw_s);
void fl_net_wifi_netdev_down(void);

fl_result_t fl_net_wifi_netdev_ipv4(uint32_t *addr_be_out);

/** Router DHCP profile (simulated AP LAN); distinct from host machine addresses. */
int fl_net_wifi_netdev_router_info(fl_net_dhcp_lease_info_t *out);

void fl_net_wifi_netdev_apply_dhcp_lease(const fl_net_dhcp_lease_info_t *lease);

/** Register router-assigned global IPv6 on the WLAN netdev FIB. */
fl_result_t fl_net_wifi_netdev_add_ipv6(const uint8_t src6[16], uint8_t prefix_len);

fl_result_t fl_net_wifi_netdev_ipv6(uint8_t addr6[16], uint8_t *prefix_len_out);

/** AP BSSID and STA MAC while associated (for L2 egress). */
int fl_net_wifi_netdev_peer_mac(uint8_t mac[6]);
int fl_net_wifi_netdev_sta_mac(uint8_t mac[6]);

/**
 * Lab DHCP exchange over the in-tree WiFi netdev (DISCOVER/REQUEST → OFFER/ACK).
 * Used by **fl_net_dhcp_acquire** when **drv** is the WiFi station netdev.
 */
fl_result_t fl_net_dhcp_wifi_netdev_exchange(fl_net_driver_t *drv,
                                            const uint8_t cli_mac[FL_NET_ETH_ADDR_LEN],
                                            const fl_net_packet_t *req_pkt,
                                            uint8_t *reply_l4, size_t reply_cap,
                                            size_t *reply_len, unsigned timeout_ms);

#endif /* NET_WIFI_NETDEV_H */
