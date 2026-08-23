#ifndef NET_WIFI_MGMT_OTA_H
#define NET_WIFI_MGMT_OTA_H

/**
 * Management-frame OTA orchestration (#328 Task 2).
 * Wires net_wifi_mgmt builders/parsers to nl80211 TX/RX on the FullMAC path.
 */

#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fl_net_wifi_nl80211 fl_net_wifi_nl80211_t;

typedef struct fl_net_wifi_mgmt_ota fl_net_wifi_mgmt_ota_t;

fl_result_t fl_net_wifi_mgmt_ota_create(fl_net_wifi_mgmt_ota_t **out, fl_net_wifi_nl80211_t *nl,
                                        const uint8_t sta_mac[6],
                                        const fl_net_wifi_he_cap_t *phy_he);

void fl_net_wifi_mgmt_ota_destroy(fl_net_wifi_mgmt_ota_t *ota);

/** Register Probe Response, Beacon, and Authentication RX filters. */
fl_result_t fl_net_wifi_mgmt_ota_register(fl_net_wifi_mgmt_ota_t *ota);

/** Build + transmit a Probe Request for **ssid** via nl80211. */
fl_result_t fl_net_wifi_mgmt_ota_probe_tx(fl_net_wifi_mgmt_ota_t *ota, const char *ssid,
                                          unsigned timeout_ms);

/** Merge OTA-parsed BSS cache into nl80211 scan results (same BSSID). */
void fl_net_wifi_mgmt_ota_merge_scan(fl_net_wifi_mgmt_ota_t *ota,
                                     fl_net_wifi_scan_entry_t *entries, size_t count);

/** Parse Association Response and store negotiated HE for fl_net_wifi_he_cap(). */
fl_result_t fl_net_wifi_mgmt_ota_store_assoc_resp(fl_net_wifi_mgmt_ota_t *ota,
                                                  const uint8_t *frame, size_t len);

#endif /* NET_WIFI_MGMT_OTA_H */
