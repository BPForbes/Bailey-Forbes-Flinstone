#ifndef NET_WIFI_SAE_H
#define NET_WIFI_SAE_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase,
                                       uint8_t *pmk_out, size_t pmk_cap);

/** RFC 7664 / SAE KDF self-test (hosted CI). */
fl_result_t fl_net_wifi_sae_rfc7664_kdf_selftest(void);

#endif /* NET_WIFI_SAE_H */
