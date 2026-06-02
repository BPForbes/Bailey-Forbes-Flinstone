#ifndef NET_WIFI_WPA_H
#define NET_WIFI_WPA_H

#include "contract_result.h"
#include "net_wifi_crypto.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_wifi_wpa_psk_pmk(const char *ssid, const char *passphrase,
                                    uint8_t pmk_out[FL_NET_WIFI_PMK_LEN]);

fl_result_t fl_net_wifi_wpa_ptk_from_4way(const uint8_t pmk[FL_NET_WIFI_PMK_LEN],
                                          const uint8_t anonce[32], const uint8_t snonce[32],
                                          const uint8_t bssid[6], const uint8_t sta[6],
                                          uint8_t ptk_out[FL_NET_WIFI_PTK_LEN]);

fl_result_t fl_net_wifi_wpa4_install_ptk(const uint8_t *pmk, size_t pmk_len);

int fl_net_wifi_wpa_lab_ptk_installed(void);
void fl_net_wifi_wpa_lab_reset(void);

#endif /* NET_WIFI_WPA_H */
