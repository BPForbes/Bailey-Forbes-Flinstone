#ifndef NET_WIFI_FULLMAC_H
#define NET_WIFI_FULLMAC_H

/**
 * FullMAC NIC shim (#328 Task 1.2).
 *
 * **Lab** (`FL_NET_WIFI_FULLMAC_LAB=1`): packet-validated netdev ring — same egress/channel
 * path as mock, no kernel L2 socket.
 *
 * **Physical** (`FL_NET_WIFI_NL80211=1` / `FL_NET_WIFI_IFACE`): nl80211 control plane +
 * AF_PACKET data via driver-layer wifi_host_wire.h / wifi_fullmac_afpacket.
 */

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_WIFI_IFNAME_MAX 16u

typedef struct fl_net_wifi_nl80211 fl_net_wifi_nl80211_t;
typedef struct fl_net_wifi_mgmt_ota fl_net_wifi_mgmt_ota_t;

typedef enum {
	FL_NET_WIFI_FULLMAC_MODE_NONE = 0,
	FL_NET_WIFI_FULLMAC_MODE_LAB,
	FL_NET_WIFI_FULLMAC_MODE_PHYSICAL,
} fl_net_wifi_fullmac_mode_t;

int fl_net_wifi_fullmac_available(void);

fl_result_t fl_net_wifi_fullmac_init(const char *ifname);
void fl_net_wifi_fullmac_deinit(void);

int fl_net_wifi_fullmac_active(void);

fl_net_wifi_fullmac_mode_t fl_net_wifi_fullmac_mode(void);
int fl_net_wifi_fullmac_is_lab(void);
int fl_net_wifi_fullmac_is_physical(void);

fl_net_driver_t *fl_net_wifi_fullmac_driver(void);

const char *fl_net_wifi_fullmac_ifname(void);

/** Station MAC from nl80211 (physical) or lab shim; zero when inactive. */
fl_result_t fl_net_wifi_fullmac_sta_mac(uint8_t mac_out[6]);

/** Physical mode only; NULL in lab mode. */
fl_net_wifi_nl80211_t *fl_net_wifi_fullmac_nl80211(void);

/** Lab mode: enqueue one Ethernet frame for the next recv(). */
fl_result_t fl_net_wifi_fullmac_lab_inject_rx(const uint8_t *frame, size_t len);

/** **ssid** NULL = wildcard scan; non-NULL = directed scan (1–32 byte SSID). */
fl_result_t fl_net_wifi_fullmac_scan(uint8_t band, const char *ssid, unsigned timeout_ms);
fl_result_t fl_net_wifi_fullmac_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                            size_t *count_out);

fl_result_t fl_net_wifi_fullmac_mgmt_ota_attach(fl_net_wifi_nl80211_t *nl,
                                                const uint8_t sta_mac[6],
                                                const fl_net_wifi_he_cap_t *phy_he);
void fl_net_wifi_fullmac_mgmt_ota_detach(void);
fl_net_wifi_mgmt_ota_t *fl_net_wifi_fullmac_mgmt_ota(void);

fl_result_t fl_net_wifi_fullmac_disconnect(void);

fl_result_t fl_net_wifi_fullmac_he_cap(fl_net_wifi_he_cap_t *cap_out);

int fl_net_wifi_fullmac_mt7921_detected(void);

void fl_net_wifi_fullmac_set_negotiated_he(const fl_net_wifi_he_cap_t *cap);

#endif /* NET_WIFI_FULLMAC_H */
