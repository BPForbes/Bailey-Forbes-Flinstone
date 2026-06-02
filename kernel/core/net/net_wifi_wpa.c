#include "net_wifi_wpa.h"

#include "net_wifi_crypto.h"

#include <string.h>

static uint8_t s_lab_ptk[FL_NET_WIFI_PTK_LEN];
static int s_lab_ptk_valid;

fl_result_t fl_net_wifi_wpa_psk_pmk(const char *ssid, const char *passphrase,
                                    uint8_t pmk_out[FL_NET_WIFI_PMK_LEN]) {
    return fl_net_wifi_crypto_psk_pmk(ssid, passphrase, pmk_out);
}

fl_result_t fl_net_wifi_wpa_ptk_from_4way(const uint8_t pmk[FL_NET_WIFI_PMK_LEN],
                                          const uint8_t anonce[32], const uint8_t snonce[32],
                                          const uint8_t bssid[6], const uint8_t sta[6],
                                          uint8_t ptk_out[FL_NET_WIFI_PTK_LEN]) {
    return fl_net_wifi_crypto_ptk(pmk, anonce, snonce, bssid, sta, ptk_out);
}

fl_result_t fl_net_wifi_wpa4_install_ptk(const uint8_t *pmk, size_t pmk_len) {
    static const uint8_t anonce[32] = {0x01};
    static const uint8_t snonce[32] = {0x02};
    static const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    static const uint8_t sta[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t pmk_buf[FL_NET_WIFI_PMK_LEN];

    if (!pmk || pmk_len != FL_NET_WIFI_PMK_LEN)
        return FL_RESULT_INVAL;
    memcpy(pmk_buf, pmk, FL_NET_WIFI_PMK_LEN);
    if (fl_net_wifi_wpa_ptk_from_4way(pmk_buf, anonce, snonce, bssid, sta, s_lab_ptk) !=
        FL_RESULT_OK) {
        fl_net_wifi_crypto_memzero(pmk_buf, sizeof(pmk_buf));
        return FL_RESULT_ERR;
    }
    fl_net_wifi_crypto_memzero(pmk_buf, sizeof(pmk_buf));
    s_lab_ptk_valid = 1;
    return FL_RESULT_OK;
}

int fl_net_wifi_wpa_lab_ptk_installed(void) {
    return s_lab_ptk_valid;
}

void fl_net_wifi_wpa_lab_reset(void) {
    fl_net_wifi_crypto_memzero(s_lab_ptk, sizeof(s_lab_ptk));
    s_lab_ptk_valid = 0;
}
