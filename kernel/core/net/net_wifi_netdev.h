#ifndef FL_NET_WIFI_NETDEV_H
#define FL_NET_WIFI_NETDEV_H

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "fl/driver/net.h"
#include "net_dhcp.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_WIFI_STATION_IFNAME "wlan-lab"

typedef struct fl_net_wifi_l3_profile {
    uint32_t ipv4;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
} fl_net_wifi_l3_profile_t;

/**
 * In-tree 802.11 station netdev: L2 TX/RX once associated.
 * Lab station L3 comes from simulated router DHCP (wifi_lab_router driver).
 */

fl_net_driver_t *fl_net_wifi_netdev_driver(void);
int fl_net_wifi_netdev_is_up(void);
const char *fl_net_wifi_netdev_iface(void);

fl_result_t fl_net_wifi_netdev_up(const fl_net_wifi_scan_entry_t *ap,
                                  const uint8_t sta_mac[6]);

fl_result_t fl_net_wifi_netdev_up_with_ipv4(const fl_net_wifi_scan_entry_t *ap,
                                            const uint8_t sta_mac[6],
                                            const char *addr_s, uint8_t prefix_len,
                                            const char *gw_s);
void fl_net_wifi_netdev_down(void);

fl_result_t fl_net_wifi_netdev_ipv4(uint32_t *addr_be_out);
void fl_net_wifi_netdev_apply_dhcp_lease(const fl_net_dhcp_lease_info_t *lease);
int fl_net_wifi_netdev_l3_profile(fl_net_wifi_l3_profile_t *out);

fl_result_t fl_net_wifi_netdev_add_ipv6(const uint8_t src6[16], uint8_t prefix_len);
fl_result_t fl_net_wifi_netdev_ipv6(uint8_t addr6[16], uint8_t *prefix_len_out);

int fl_net_wifi_netdev_peer_mac(uint8_t mac[6]);
int fl_net_wifi_netdev_sta_mac(uint8_t mac[6]);

#endif
