#ifndef NET_WIFI_CRYPTO_H
#define NET_WIFI_CRYPTO_H

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

#define FL_NET_WIFI_PMK_LEN 32u
#define FL_NET_WIFI_PTK_LEN 48u

/** WPA2-PSK PMK (802.11i PBKDF2-SHA1, 4096 iterations). */
fl_result_t fl_net_wifi_crypto_psk_pmk(const char *ssid, const char *passphrase,
                                       uint8_t pmk_out[FL_NET_WIFI_PMK_LEN]);

/**
 * WPA2 PTK from PMK + 4-way nonces (802.11i PRF-512, first 48 bytes).
 * **anonce** / **snonce** are 32 bytes; **bssid** and **sta** are 6-byte MACs.
 */
fl_result_t fl_net_wifi_crypto_ptk(const uint8_t pmk[FL_NET_WIFI_PMK_LEN],
                                   const uint8_t anonce[32], const uint8_t snonce[32],
                                   const uint8_t bssid[6], const uint8_t sta[6],
                                   uint8_t ptk_out[FL_NET_WIFI_PTK_LEN]);

/** SAE KDF (802.11-2020 12.4.2): HMAC-SHA256(key, 0 || label || context || len). */
fl_result_t fl_net_wifi_crypto_sae_kdf(const uint8_t *key, size_t key_len, const char *label,
                                       const uint8_t *context, size_t context_len,
                                       uint8_t *out, size_t out_len);

void fl_net_wifi_crypto_memzero(void *p, size_t n);

#endif /* NET_WIFI_CRYPTO_H */
