#ifndef WIFI_NL80211_H
#define WIFI_NL80211_H

/**
 * Hosted Linux nl80211 management-frame backend (#328 Task 1.1).
 * Driver-layer execution; uses wifi_host_wire.h (no Linux kernel headers).
 */

#include "contract_p3_wifi.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

typedef void (*fl_net_wifi_nl80211_mgmt_cb_t)(const uint8_t *frame, size_t len, void *ctx);

typedef struct fl_net_wifi_nl80211 fl_net_wifi_nl80211_t;

fl_result_t fl_net_wifi_nl80211_init(fl_net_wifi_nl80211_t **out, const char *ifname);
void fl_net_wifi_nl80211_deinit(fl_net_wifi_nl80211_t *nl);

int fl_net_wifi_nl80211_available(void);

fl_result_t fl_net_wifi_nl80211_ifindex(const fl_net_wifi_nl80211_t *nl, uint32_t *ifindex_out);

fl_result_t fl_net_wifi_nl80211_sta_mac(const fl_net_wifi_nl80211_t *nl, uint8_t mac_out[6]);

fl_result_t fl_net_wifi_nl80211_mgmt_tx(fl_net_wifi_nl80211_t *nl, const uint8_t *frame,
                                        size_t len, unsigned timeout_ms);

fl_result_t fl_net_wifi_nl80211_register_mgmt(fl_net_wifi_nl80211_t *nl, uint16_t frame_type,
                                              fl_net_wifi_nl80211_mgmt_cb_t cb, void *ctx);

fl_result_t fl_net_wifi_nl80211_mgmt_rx(fl_net_wifi_nl80211_t *nl, uint8_t *frame, size_t cap,
                                        size_t *len_out, unsigned timeout_ms);

fl_result_t fl_net_wifi_nl80211_poll(fl_net_wifi_nl80211_t *nl, unsigned timeout_ms);

fl_result_t fl_net_wifi_nl80211_trigger_scan(fl_net_wifi_nl80211_t *nl, const char *ssid);

fl_result_t fl_net_wifi_nl80211_get_scan(fl_net_wifi_nl80211_t *nl,
                                         fl_net_wifi_scan_entry_t *entries, size_t cap,
                                         size_t *count_out);

fl_result_t fl_net_wifi_nl80211_get_wiphy_caps(fl_net_wifi_nl80211_t *nl,
                                               fl_net_wifi_he_cap_t *he_out, uint8_t *bands_out);

fl_result_t fl_net_wifi_nl80211_install_key(fl_net_wifi_nl80211_t *nl, uint8_t key_index,
                                            const uint8_t *key, size_t key_len, int pairwise);

int fl_net_wifi_nl80211_mt7921_detected(const fl_net_wifi_nl80211_t *nl);

#endif /* WIFI_NL80211_H */
