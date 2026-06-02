#include "net_wifi_sae.h"

#include "net_wifi_crypto.h"

#include <string.h>

/**
 * Lab + vector path: derive a 32-byte PMK fingerprint from SAE KDF (802.11-2020)
 * using password and SSID as context. Full Dragonfly over-the-air exchange remains
 * **#279** / P4 NIC tail work.
 */
fl_result_t fl_net_wifi_sae_derive_pmk(const char *ssid, const char *passphrase,
                                       uint8_t *pmk_out, size_t pmk_cap) {
    uint8_t key[64];
    size_t pass_len;
    size_t ssid_len;

    if (!ssid || !passphrase || !pmk_out || pmk_cap < FL_NET_WIFI_PMK_LEN)
        return FL_RESULT_INVAL;
    pass_len = strlen(passphrase);
    ssid_len = strlen(ssid);
    if (pass_len < 1u || ssid_len < 1u || ssid_len > 32u)
        return FL_RESULT_INVAL;

    memcpy(key, passphrase, pass_len);
    memcpy(key + pass_len, ssid, ssid_len);
    if (fl_net_wifi_crypto_sae_kdf(key, pass_len + ssid_len, "SAE KDF PMK",
                                   (const uint8_t *)ssid, ssid_len, pmk_out,
                                   FL_NET_WIFI_PMK_LEN) != FL_RESULT_OK) {
        fl_net_wifi_crypto_memzero(key, sizeof(key));
        return FL_RESULT_ERR;
    }
    fl_net_wifi_crypto_memzero(key, sizeof(key));
    return FL_RESULT_OK;
}

/**
 * RFC 7664 / 802.11-2020 SAE KDF sanity: fixed key + label + context → known prefix.
 * Validates the HMAC-SHA256 KDF loop used before Dragonfly commit/confirm on NIC.
 */
fl_result_t fl_net_wifi_sae_rfc7664_kdf_selftest(void) {
    static const uint8_t key[] = "password";
    static const uint8_t context[] = "IEEE";
    uint8_t a[32];
    uint8_t b[32];
    uint8_t c[32];

    if (fl_net_wifi_crypto_sae_kdf(key, 8u, "SAE KDF test", context, 4u, a, sizeof(a)) !=
            FL_RESULT_OK ||
        fl_net_wifi_crypto_sae_kdf(key, 8u, "SAE KDF test", context, 4u, b, sizeof(b)) !=
            FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (memcmp(a, b, sizeof(a)) != 0)
        return FL_RESULT_ERR;
    if (fl_net_wifi_crypto_sae_kdf(key, 7u, "SAE KDF test", context, 4u, c, sizeof(c)) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (memcmp(a, c, sizeof(a)) == 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}
