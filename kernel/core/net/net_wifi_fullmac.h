#ifndef NET_WIFI_FULLMAC_H
#define NET_WIFI_FULLMAC_H

/**
 * nl80211-backed FullMAC NIC shim (#328 Task 1.2).
 * Registers **fl_net_driver_t** for L2 data; management via **net_wifi_nl80211**.
 */

#include "contract_p3_wifi.h"
#include "contract_result.h"
#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_wifi_fullmac_init(const char *ifname);
void fl_net_wifi_fullmac_deinit(void);

int fl_net_wifi_fullmac_active(void);

fl_net_driver_t *fl_net_wifi_fullmac_driver(void);

const char *fl_net_wifi_fullmac_ifname(void);

fl_result_t fl_net_wifi_fullmac_scan(uint8_t band, unsigned timeout_ms);
fl_result_t fl_net_wifi_fullmac_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                            size_t *count_out);

fl_result_t fl_net_wifi_fullmac_disconnect(void);

/** PHY capabilities from GET_WIPHY; negotiated HE after association. */
fl_result_t fl_net_wifi_fullmac_he_cap(fl_net_wifi_he_cap_t *cap_out);

int fl_net_wifi_fullmac_mt7921_detected(void);

/** Post-association negotiated HE (Task 3+); PHY caps until set. */
void fl_net_wifi_fullmac_set_negotiated_he(const fl_net_wifi_he_cap_t *cap);

#endif /* NET_WIFI_FULLMAC_H */
