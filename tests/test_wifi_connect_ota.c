/**
 * #328 — SAE / EAPOL / TWT connect OTA unit tests (mock mgmt transport).
 */
#include "contract_p3_wifi.h"
#include "wifi_connect_ota.h"
#include "wifi_coprocessor.h"
#include "wifi_mgmt_transport.h"
#include "wifi_twt_ota.h"
#include "wifi_supplicant.h"

#include "net_wifi_fullmac.h"
#include "net_wifi_mgmt_ota.h"
#include "contract_result.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static const uint8_t k_sta[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t k_bssid[6] = {0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01};

/* Test-only: avoid nl80211/fullmac shim in this focused OTA unit binary. */
fl_net_wifi_mgmt_ota_t *fl_net_wifi_fullmac_mgmt_ota(void) { return NULL; }
fl_result_t fl_net_wifi_mgmt_ota_store_assoc_resp(fl_net_wifi_mgmt_ota_t *ota, const uint8_t *frame,
                                                  size_t len) {
    (void)ota;
    (void)frame;
    (void)len;
    return FL_RESULT_NOSYS;
}
void fl_net_wifi_fullmac_set_negotiated_he(const fl_net_wifi_he_cap_t *he) { (void)he; }

static wifi_network_t mock_ap_wpa3(void) {
    wifi_network_t ap;

    memset(&ap, 0, sizeof(ap));
    memcpy(ap.bssid, k_bssid, 6);
    ap.auth_mode = WIFI_AUTH_WPA3_SAE;
    strncpy(ap.ssid, "OtaSae6", sizeof(ap.ssid) - 1u);
    return ap;
}

static wifi_network_t mock_ap_wpa2(void) {
    wifi_network_t ap;

    memset(&ap, 0, sizeof(ap));
    memcpy(ap.bssid, k_bssid, 6);
    ap.auth_mode = WIFI_AUTH_WPA2_PSK;
    strncpy(ap.ssid, "OtaWpa2", sizeof(ap.ssid) - 1u);
    return ap;
}

static int init_transport(wifi_network_t *ap, wifi_mgmt_transport_t *tr,
                          wifi_mgmt_transport_mock_ctx_t *storage) {
    wifi_mgmt_transport_mock_cfg_t cfg = {.ap = ap, .sta_mac = k_sta};

    return wifi_mgmt_transport_mock_init(tr, storage, &cfg);
}

static int test_sae_ota_exchange(void) {
    wifi_network_t ap = mock_ap_wpa3();
    wifi_mgmt_transport_mock_ctx_t storage;
    wifi_mgmt_transport_t tr;
    fl_net_wifi_cred_t cred;

    ASSERT(init_transport(&ap, &tr, &storage) == 0);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, ap.ssid, sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "mock-secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(wifi_connect_ota_run(&cred, &ap, k_sta, &tr, NULL) == 0);
    printf("ok #328 sae-ota-exchange\n");
    return 0;
}

static int test_sae_anticlogging_retry(void) {
    wifi_network_t ap = mock_ap_wpa3();
    wifi_mgmt_transport_mock_ctx_t storage;
    wifi_mgmt_transport_t tr;
    fl_net_wifi_cred_t cred;

    ASSERT(init_transport(&ap, &tr, &storage) == 0);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, ap.ssid, sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "mock-secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(wifi_connect_ota_run_phase(&cred, &ap, k_sta, &tr, NULL,
                                      WIFI_CONNECT_OTA_AUTH_ONLY) == 0);
    printf("ok #328 sae-anticlogging-token\n");
    return 0;
}

static int test_eapol_ota_exchange(void) {
    wifi_network_t ap = mock_ap_wpa2();
    wifi_mgmt_transport_mock_ctx_t storage;
    wifi_mgmt_transport_t tr;
    fl_net_wifi_cred_t cred;

    ASSERT(init_transport(&ap, &tr, &storage) == 0);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, ap.ssid, sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "mock-secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA2_PSK;
    ASSERT(wifi_connect_ota_run(&cred, &ap, k_sta, &tr, NULL) == 0);
    printf("ok #328 eapol-4way-ota\n");
    return 0;
}

static int test_twt_ota_setup_teardown(void) {
    wifi_network_t ap = mock_ap_wpa3();
    wifi_mgmt_transport_mock_ctx_t storage;
    wifi_mgmt_transport_t tr;
    fl_net_wifi_twt_params_t req = {.wake_duration_us = 8000u, .wake_interval_us = 100000u};
    fl_net_wifi_twt_params_t agreed = {0};

    ASSERT(init_transport(&ap, &tr, &storage) == 0);
    ASSERT(wifi_twt_ota_setup(k_sta, k_bssid, &req, &agreed, &tr) == 0);
    ASSERT(agreed.flow_id < 8u);
    ASSERT(agreed.wake_duration_us == 8000u);
    ASSERT(wifi_twt_ota_teardown(k_sta, k_bssid, agreed.flow_id, &tr) == 0);
    printf("ok #328 twt-ota-setup-teardown\n");
    return 0;
}

int main(void) {
    int rc = 0;

    if (test_sae_ota_exchange() != 0)
        rc = 1;
    if (test_sae_anticlogging_retry() != 0)
        rc = 1;
    if (test_eapol_ota_exchange() != 0)
        rc = 1;
    if (test_twt_ota_setup_teardown() != 0)
        rc = 1;
    return rc;
}
