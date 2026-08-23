#include "net_wifi_crypto.h"

#include "fl/mem_asm.h"
#include "net_endian.h"

#include <limits.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <string.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

void fl_net_wifi_crypto_memzero(void *p, size_t n) {
    if (!p || n == 0u)
        return;
    asm_mem_zero(p, n);
}

fl_result_t fl_net_wifi_crypto_random(uint8_t *out, size_t len) {
#if defined(__linux__)
    ssize_t got;
#endif

    if (!out || len == 0u)
        return FL_RESULT_INVAL;
#if defined(__linux__)
    got = getrandom(out, len, 0);
    if (got == (ssize_t)len)
        return FL_RESULT_OK;
#endif
    if (len > (size_t)INT_MAX)
        return FL_RESULT_ERR;
    if (RAND_bytes(out, (int)len) == 1)
        return FL_RESULT_OK;
    return FL_RESULT_ERR;
}

fl_result_t fl_net_wifi_crypto_psk_pmk(const char *ssid, const char *passphrase,
                                       uint8_t pmk_out[FL_NET_WIFI_PMK_LEN]) {
    size_t ssid_len;
    size_t pass_len;

    if (!ssid || !passphrase || !pmk_out)
        return FL_RESULT_INVAL;
    ssid_len = strlen(ssid);
    pass_len = strlen(passphrase);
    if (ssid_len == 0u || ssid_len > 32u || pass_len < 8u || pass_len > 63u)
        return FL_RESULT_INVAL;
    if (PKCS5_PBKDF2_HMAC_SHA1(passphrase, (int)pass_len, (const unsigned char *)ssid,
                               (int)ssid_len, 4096, (int)FL_NET_WIFI_PMK_LEN, pmk_out) != 1)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t ieee80211_prf(const uint8_t *key, size_t key_len, const char *label,
                                   const uint8_t *data, size_t data_len, uint8_t *out,
                                   size_t out_len) {
    uint8_t msg[256];
    size_t label_len;
    size_t pos;
    size_t written = 0u;
    uint8_t counter = 0u;
    uint16_t bits;

    if (!key || key_len == 0u || !label || !data || !out || out_len == 0u)
        return FL_RESULT_INVAL;

    label_len = strlen(label);
    bits = (uint16_t)(out_len * 8u);
    if (1u + label_len + data_len + 2u > sizeof(msg))
        return FL_RESULT_INVAL;

    while (written < out_len) {
        unsigned char mac[20];
        unsigned int mac_len = 0u;

        pos = 0u;
        msg[pos++] = counter;
        memcpy(msg + pos, label, label_len);
        pos += label_len;
        memcpy(msg + pos, data, data_len);
        pos += data_len;
        msg[pos++] = (uint8_t)(bits & 0xffu);
        msg[pos++] = (uint8_t)((bits >> 8) & 0xffu);

        if (HMAC(EVP_sha1(), key, (int)key_len, msg, pos, mac, &mac_len) == NULL ||
            mac_len != 20u)
            return FL_RESULT_ERR;
        {
            size_t chunk = out_len - written;
            if (chunk > 20u)
                chunk = 20u;
            memcpy(out + written, mac, chunk);
            written += chunk;
        }
        counter++;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_crypto_ptk(const uint8_t pmk[FL_NET_WIFI_PMK_LEN],
                                   const uint8_t anonce[32], const uint8_t snonce[32],
                                   const uint8_t bssid[6], const uint8_t sta[6],
                                   uint8_t ptk_out[FL_NET_WIFI_PTK_LEN]) {
    uint8_t data[76];
    const uint8_t *addr1;
    const uint8_t *addr2;
    const uint8_t *nonce1;
    const uint8_t *nonce2;

    if (!pmk || !anonce || !snonce || !bssid || !sta || !ptk_out)
        return FL_RESULT_INVAL;

    if (memcmp(bssid, sta, 6) < 0) {
        addr1 = bssid;
        addr2 = sta;
    } else {
        addr1 = sta;
        addr2 = bssid;
    }
    if (memcmp(anonce, snonce, 32) < 0) {
        nonce1 = anonce;
        nonce2 = snonce;
    } else {
        nonce1 = snonce;
        nonce2 = anonce;
    }
    memcpy(data, addr1, 6u);
    memcpy(data + 6, addr2, 6u);
    memcpy(data + 12, nonce1, 32u);
    memcpy(data + 44, nonce2, 32u);

    return ieee80211_prf(pmk, FL_NET_WIFI_PMK_LEN, "Pairwise key expansion", data, 76u,
                         ptk_out, FL_NET_WIFI_PTK_LEN);
}

fl_result_t fl_net_wifi_crypto_sae_kdf(const uint8_t *key, size_t key_len, const char *label,
                                       const uint8_t *context, size_t context_len,
                                       uint8_t *out, size_t out_len) {
    uint8_t buf[256];
    size_t label_len;
    size_t pos = 0u;
    size_t written = 0u;
    unsigned int mac_len = 0u;

    if (!key || key_len == 0u || !label || !out || out_len == 0u)
        return FL_RESULT_INVAL;
    if (out_len > 255u * 32u)
        return FL_RESULT_INVAL;

    label_len = strlen(label);
    if (label_len + 2u + context_len + 2u > sizeof(buf))
        return FL_RESULT_INVAL;

    buf[pos++] = 0u;
    memcpy(buf + pos, label, label_len);
    pos += label_len;
    if (context_len > 0u) {
        memcpy(buf + pos, context, context_len);
        pos += context_len;
    }
    buf[pos++] = (uint8_t)(out_len & 0xffu);

    {
        uint8_t iter = 0u;
        while (written < out_len) {
            uint8_t mac[32];
            buf[0] = iter;
            if (HMAC(EVP_sha256(), key, (int)key_len, buf, pos, mac, &mac_len) == NULL ||
                mac_len != 32u)
                return FL_RESULT_ERR;
            {
                size_t chunk = out_len - written;
                if (chunk > 32u)
                    chunk = 32u;
                memcpy(out + written, mac, chunk);
                written += chunk;
            }
            iter++;
        }
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_crypto_ieee80211_kdf_sha256(const uint8_t *key, size_t key_len,
                                                    const char *label, const uint8_t *context,
                                                    size_t context_len, uint8_t *out,
                                                    size_t out_len) {
    uint8_t buf[256];
    size_t label_len;
    size_t pos;
    size_t written = 0u;
    uint16_t counter = 1u;
    uint16_t bits;

    if (!key || key_len == 0u || !label || !out || out_len == 0u)
        return FL_RESULT_INVAL;
    if (out_len > (65535u / 8u))
        return FL_RESULT_INVAL;
    if (context_len > 0u && !context)
        return FL_RESULT_INVAL;

    label_len = strlen(label);
    if (2u + label_len + 1u + context_len + 2u > sizeof(buf))
        return FL_RESULT_INVAL;

    bits = (uint16_t)(out_len * 8u);
    pos = 2u;
    memcpy(buf + pos, label, label_len);
    pos += label_len;
    buf[pos++] = 0u;
    if (context_len > 0u) {
        memcpy(buf + pos, context, context_len);
        pos += context_len;
    }
    fl_net_put_u16_le(buf + pos, bits);
    pos += 2u;

    while (written < out_len) {
        uint8_t mac[32];
        unsigned int mac_len = 0u;
        size_t chunk;

        fl_net_put_u16_le(buf, counter);
        if (HMAC(EVP_sha256(), key, (int)key_len, buf, pos, mac, &mac_len) == NULL ||
            mac_len != 32u)
            return FL_RESULT_ERR;
        chunk = out_len - written;
        if (chunk > 32u)
            chunk = 32u;
        memcpy(out + written, mac, chunk);
        written += chunk;
        if (counter == 0xffffu)
            return FL_RESULT_ERR;
        counter++;
    }
    return FL_RESULT_OK;
}
