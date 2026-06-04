#ifndef NET_WIFI_NETDEV_H
#define NET_WIFI_NETDEV_H

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "fl/driver/net.h"

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
void fl_net_wifi_netdev_down(void);

fl_result_t fl_net_wifi_netdev_ipv4(uint32_t *addr_be_out);

#endif /* NET_WIFI_NETDEV_H */
