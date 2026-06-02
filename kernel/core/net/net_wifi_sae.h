#ifndef NET_WIFI_SAE_H
#define NET_WIFI_SAE_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

/**
 * WPA3-SAE (Dragonfly) — production exchange is **#279** tail work.
 * Returns **FL_RESULT_NOSYS** until P4/NIC + crypto backend land.
 */
fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase,
                                       uint8_t *pmk_out, size_t pmk_cap);

#endif /* NET_WIFI_SAE_H */
