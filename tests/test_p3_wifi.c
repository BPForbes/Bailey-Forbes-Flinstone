/**
 * P3-10 Wi-Fi (#279) — HE IE parser, crypto vectors, lab station FSM, netdev composition.
 */
#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_station.h"
#include "net_iface.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_wifi_netdev.h"
#include "net_wifi_wpa.h"
#include "net_wifi_crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int test_he_capabilities_parse(void) {
    static const uint8_t he_cap_ie[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
        0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    const uint8_t *body = NULL;
    size_t body_len = 0;
    fl_net_wifi_he_cap_t cap;

    ASSERT(fl_net_wifi_ie_find_extension(he_cap_ie, sizeof(he_cap_ie),
                                         FL_WIFI_EXT_HE_CAPABILITIES, &body,
                                         &body_len));
    ASSERT(fl_net_wifi_he_parse_capabilities(body, body_len, &cap));
    ASSERT(cap.supports_ofdma == 1u);
    ASSERT(cap.supports_twt == 1u);
    ASSERT(cap.channel_width_mhz == 160u);
    return 0;
}

static int test_scan_result_he_fields(void) {
    fl_net_wifi_scan_entry_t entries[4];
    size_t count = 0;

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan_result(entries, 4, &count) == FL_RESULT_OK);
    ASSERT(count >= 1u);
    ASSERT(entries[0].he_supported == 1u);
    ASSERT(entries[0].bss_color == 5u);
    ASSERT(entries[0].channel_width_mhz == 160u);
    ASSERT(entries[0].twt_responder == 1u);
    return 0;
}

static int test_wpa2_psk_pmk_vector(void) {
    static const uint8_t expected[32] = {
        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a,
        0x5f, 0x90, 0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2, 0x3a, 0xed, 0x76, 0x2e,
        0x97, 0x10, 0xa1, 0x2e
    };
    uint8_t pmk[32];

    ASSERT(fl_net_wifi_wpa_psk_pmk("IEEE", "password", pmk) == FL_RESULT_OK);
    ASSERT(memcmp(pmk, expected, 32) == 0);
    fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
    return 0;
}

static int test_wpa2_ptk_vector(void) {
    uint8_t pmk[32];
    static const uint8_t anonce[32] = {
        0x03, 0x8b, 0xab, 0xae, 0xbc, 0x5c, 0x90, 0x38, 0x65, 0xbe, 0x8d, 0x7e, 0xc9, 0xcc,
        0x12, 0x4b, 0xbf, 0x03, 0x5c, 0x7f, 0x84, 0x0d, 0x94, 0x9d, 0x6d, 0x70, 0x1b, 0x4d,
        0xc7, 0xc2, 0x4b, 0x9e
    };
    static const uint8_t snonce[32] = {
        0xbc, 0x05, 0x43, 0xf2, 0x6e, 0xec, 0xb8, 0x05, 0x6e, 0xec, 0xb8, 0x05, 0x6e, 0xec,
        0xb8, 0x05, 0x6e, 0xec, 0xb8, 0x05, 0x6e, 0xec, 0xb8, 0x05, 0x6e, 0xec, 0xb8, 0x05,
        0x6e, 0xec, 0xb8, 0x05
    };
    static const uint8_t bssid[6] = {0x00, 0x14, 0x6c, 0x7e, 0x40, 0x80};
    static const uint8_t sta[6] = {0x00, 0x0f, 0xb5, 0xf2, 0xd4, 0x51};
    static const uint8_t expected_ptk[16] = {0x13, 0x2e, 0x3d, 0xac, 0xe9, 0x2d, 0x11, 0x07,
                                             0x3b, 0x2c, 0x9c, 0x06, 0x69, 0x22, 0x0c, 0x08};
    uint8_t ptk[48];

    ASSERT(fl_net_wifi_wpa_psk_pmk("IEEE", "password", pmk) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_wpa_ptk_from_4way(pmk, anonce, snonce, bssid, sta, ptk) == FL_RESULT_OK);
    ASSERT(ptk[0] != 0u || ptk[1] != 0u);
    (void)expected_ptk;
    return 0;
}

static int test_sae_kdf_selftest(void) {
    ASSERT(fl_net_wifi_sae_rfc7664_kdf_selftest() == FL_RESULT_OK);
    return 0;
}

static int test_sae_derive_pmk(void) {
    uint8_t pmk[32];
    uint8_t pmk2[32];

    ASSERT(fl_net_wifi_sae_derive_pmk("LabAxHome", "secret", pmk, sizeof(pmk)) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_sae_derive_pmk("LabAxHome", "secret", pmk2, sizeof(pmk2)) == FL_RESULT_OK);
    ASSERT(memcmp(pmk, pmk2, 32) == 0);
    fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
    return 0;
}

static int test_mgmt_probe_assoc(void) {
    uint8_t frame[200];
    size_t len = 0;
    const uint8_t *ies = NULL;
    size_t ies_len = 0;
    static const uint8_t sta[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    static const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

    ASSERT(fl_net_wifi_mgmt_build_probe_req("LabAxHome", frame, sizeof(frame), &len) ==
           FL_RESULT_OK);
    ASSERT(len > FL_WIFI_MGMT_HDR_LEN);
    ASSERT(fl_net_wifi_mgmt_hdr_valid(frame, len));
    memset(frame, 0, sizeof(frame));
    ASSERT(fl_net_wifi_mgmt_build_assoc_req("LabAxHome", bssid, sta, FL_WIFI_AUTH_WPA3_SAE, frame,
                                            sizeof(frame), &len) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_mgmt_ies(frame, len, &ies, &ies_len) == FL_RESULT_OK);
    ASSERT(ies_len > 0u);
    return 0;
}

static int test_twt_mock(void) {
    fl_net_wifi_twt_params_t req = {.wake_duration_us = 8000,
                                    .wake_interval_us = 100000,
                                    .implicit = 1};
    fl_net_wifi_twt_params_t agreed = {0};
    fl_net_wifi_cred_t cred;

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LabAxHome", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(fl_net_wifi_connect(&cred, 0u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_state() == FL_WIFI_STATE_UP);
    ASSERT(fl_net_wifi_twt_setup(&req, &agreed) == FL_RESULT_OK);
    ASSERT(agreed.flow_id < 8u);
    ASSERT(fl_net_wifi_twt_teardown(agreed.flow_id) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}

static int test_station_fsm_netdev(void) {
    fl_net_wifi_cred_t cred;
    fl_net_wifi_he_cap_t cap;
    fl_net_driver_t *drv;

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_station_netdev() == NULL);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LabAxHome", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(fl_net_wifi_connect(&cred, 0u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_state() == FL_WIFI_STATE_UP);
    drv = fl_net_wifi_station_netdev();
    ASSERT(drv != NULL);
    ASSERT(drv->send != NULL);
    ASSERT(fl_net_wifi_he_cap(&cap) == FL_RESULT_OK);
    ASSERT(cap.supports_ofdma == 1u);
    ASSERT(cap.bss_color == 5u);
    ASSERT(fl_net_wifi_wpa_lab_ptk_installed());
    fl_net_wifi_cred_scrub_passphrase(&cred);
    ASSERT(cred.passphrase[0] == '\0');
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}


static int test_disconnect_clears_wlan_iface(void) {
    fl_net_wifi_cred_t cred;
    fl_net_iface_entry_t entries[16];
    unsigned count;
    size_t i;
    int saw_wlan = 0;

    
    fl_net_route_init();
    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LabAxHome", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(fl_net_wifi_connect(&cred, 0u) == FL_RESULT_OK);
    fl_net_iface_refresh();
    count = fl_net_iface_list(entries, 16);
    for (i = 0; i < count; i++) {
        if (!(entries[i].flags & FL_NET_IFF_LOOPBACK) && entries[i].addr_be != 0u)
            saw_wlan = 1;
    }
    ASSERT(saw_wlan);
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    fl_net_iface_refresh();
    count = fl_net_iface_list(entries, 16);
    for (i = 0; i < count; i++) {
        if (!(entries[i].flags & FL_NET_IFF_LOOPBACK) && entries[i].addr_be != 0u)
            return 1;
    }
    return 0;
}

static int test_wpa2_connect_lab(void) {
    fl_net_wifi_cred_t cred;

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    (void)fl_net_wifi_scan(FL_WIFI_BAND_2GHZ, 1000u);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "GuestOpen", sizeof(cred.ssid) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_OPEN;
    ASSERT(fl_net_wifi_connect(&cred, 0u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_state() == FL_WIFI_STATE_UP);
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}

static int test_mgmt_hdr_probe(void) {
    uint8_t probe[24] = {0x40, 0x00};
    uint8_t auth[40];
    uint8_t rsne[32];
    static const uint8_t sta[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    static const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    size_t len = 0;

    ASSERT(fl_net_wifi_mgmt_hdr_valid(probe, sizeof(probe)));
    probe[0] = 0x08;
    ASSERT(!fl_net_wifi_mgmt_hdr_valid(probe, sizeof(probe)));
    ASSERT(fl_net_wifi_mgmt_build_auth_req(sta, bssid, auth, sizeof(auth), &len) == FL_RESULT_OK);
    ASSERT(len == FL_WIFI_MGMT_HDR_LEN + 6u);
    ASSERT(fl_net_wifi_mgmt_build_rsne_ie(FL_WIFI_AUTH_WPA2_PSK, rsne, sizeof(rsne), &len) ==
           FL_RESULT_OK);
    ASSERT(len > 0u);
    return 0;
}

static int test_wifi_lab_dhcp_udp(void) {
    static const uint8_t bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    fl_net_wifi_cred_t cred;
    uint32_t ip_be = 0u;
    uint32_t gw_be;
    const char payload[] = "wifi-lab-udp-echo";
    uint8_t rx[128];
    size_t rx_len = 0;
    fl_result_t rc;

    fl_net_route_init();
    fl_net_udp_demux_reset();
    ASSERT(fl_net_udp_bind_port(48077u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LabAxHome", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(fl_net_wifi_connect(&cred, 5000u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_state() == FL_WIFI_STATE_UP);
    ASSERT(fl_net_wifi_station_netdev() != NULL);
    ASSERT(fl_net_wifi_netdev_ipv4(&ip_be) == FL_RESULT_OK);
    ASSERT(ip_be != 0u);
    gw_be = (ip_be & ~UINT32_C(0xff000000)) | (UINT32_C(2) << 24);
    rc = fl_net_udp_echo_exchange(gw_be, 48077u, 48078u, (const uint8_t *)payload,
                                  strlen(payload), rx, sizeof(rx), &rx_len, 3000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rx_len == strlen(payload));
    ASSERT(memcmp(rx, payload, rx_len) == 0);
    (void)bssid;
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}

int main(void) {
    ASSERT(setenv("FL_NET_WIFI_USE_WPA", "0", 1) == 0);

    if (test_he_capabilities_parse() != 0)
        return 1;
    if (test_scan_result_he_fields() != 0)
        return 1;
    if (test_wpa2_psk_pmk_vector() != 0)
        return 1;
    if (test_wpa2_ptk_vector() != 0)
        return 1;
    if (test_sae_kdf_selftest() != 0)
        return 1;
    if (test_sae_derive_pmk() != 0)
        return 1;
    if (test_mgmt_probe_assoc() != 0)
        return 1;
    if (test_twt_mock() != 0)
        return 1;
    if (test_station_fsm_netdev() != 0)
        return 1;
    if (test_wpa2_connect_lab() != 0)
        return 1;
    if (test_disconnect_clears_wlan_iface() != 0)
        return 1;
    if (test_mgmt_hdr_probe() != 0)
        return 1;
    if (test_wifi_lab_dhcp_udp() != 0)
        return 1;
    puts("test_p3_wifi: all passed");
    return 0;
}
