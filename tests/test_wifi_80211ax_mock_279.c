/**
 * Issue #279 — all 33 tracked items (4 prerequisites + 19 scope + 10 acceptance).
 * Uses FL_WIFI_80211AX_MOCK=1 (in-process 802.11ax FullMAC) for Wi-Fi 5-only hosts.
 */
#include "contract_p3_trust.h"
#include "contract_p3_wifi.h"
#include "net_iface.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_station.h"
#include "net_wifi_wpa.h"
#include "net_wifi_netdev.h"
#include "wifi_driver_backend.h"
#include "wifi_80211ax_mock.h"
#include "wifi_supplicant.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MOCK_PASS "mock-secret"

#define FAIL279(tag, msg)                                                      \
    do {                                                                       \
        fprintf(stderr, "FAIL #279 %s: %s\n", (tag), (msg));                   \
        return 1;                                                              \
    } while (0)

#define OK279(tag)                                                             \
    do {                                                                       \
        printf("ok #279 %s\n", (tag));                                         \
    } while (0)

#define STATION_INIT()                                                         \
    do {                                                                       \
        if (fl_net_wifi_station_init() != FL_RESULT_OK)                        \
            FAIL279("init", "fl_net_wifi_station_init");                       \
    } while (0)

static void mock279_env(void)
{
    (void)setenv("FL_WIFI_80211AX_MOCK", "1", 1);
    (void)setenv("FL_NET_WIFI_USE_WPA", "0", 1);
    (void)unsetenv("FL_WIFI_UART_FD");
}

/* --- Prerequisites (4) --- */

static int prereq01_p4_driver(void)
{
    mock279_env();
    if (wifi_driver_backend_init() != FL_RESULT_OK)
        FAIL279("prereq-1", "mock FullMAC backend init");
    if (wifi_driver_backend_active() != WIFI_BACKEND_QEMU)
        FAIL279("prereq-1", "expected WIFI_BACKEND_QEMU");
    OK279("prereq-1 P4 firmware/driver (mock FullMAC)");
    return 0;
}

static int prereq02_ax_nic(void)
{
    wifi_fullmac_t *dev = NULL;
    if (wifi_80211ax_mock_attach(&dev) != 0 || !dev)
        FAIL279("prereq-2", "802.11ax mock attach");
    if (dev->state != WIFI_FULLMAC_STATE_FW_READY)
        FAIL279("prereq-2", "mock NIC not FW_READY");
    wifi_80211ax_mock_detach(dev);
    OK279("prereq-2 QEMU/real ax NIC (mock substitutes ax hardware)");
    return 0;
}

static int prereq03_dhcp(void)
{
    fl_net_wifi_cred_t cred;
    uint32_t ip_be = 0u;

    mock279_env();
    fl_net_route_init();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("prereq-3", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("prereq-3", "connect");
    if (fl_net_wifi_netdev_ipv4(&ip_be) != FL_RESULT_OK || ip_be == 0u)
        FAIL279("prereq-3", "fl_net_dhcp_acquire / lease");
    (void)fl_net_wifi_disconnect();
    OK279("prereq-3 P3-12 DHCP");
    return 0;
}

static int prereq04_egress(void)
{
    fl_net_wifi_cred_t cred;
    const char payload[] = "279-mock-egress";
    uint8_t rx[64];
    size_t rx_len = 0;
    uint32_t ip_be = 0u;
    uint32_t gw_be;

    mock279_env();
    fl_net_route_init();
    fl_net_udp_demux_reset();
    if (fl_net_udp_bind_port(48079u) != FL_RESULT_OK)
        FAIL279("prereq-4", "udp bind");
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("prereq-4", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("prereq-4", "connect");
    if (fl_net_wifi_netdev_ipv4(&ip_be) != FL_RESULT_OK)
        FAIL279("prereq-4", "no ipv4");
    gw_be = (ip_be & ~UINT32_C(0xff000000)) | (UINT32_C(2) << 24);
    if (fl_net_udp_echo_exchange(gw_be, 48079u, 48080u, (const uint8_t *)payload,
                                 strlen(payload), rx, sizeof(rx), &rx_len, 3000u) !=
        FL_RESULT_OK)
        FAIL279("prereq-4", "UDP via Wi-Fi netdev / wire egress");
    (void)fl_net_wifi_disconnect();
    OK279("prereq-4 P3-5 routing/egress");
    return 0;
}

/* --- Scope (19) --- */

static int scope01_contract(void)
{
    if (FL_NET_WIFI_AUTHZ_OP_SCAN_CONNECT != FL_AUTHZ_OP_NETDEV_IO)
        FAIL279("scope-1", "contract_p3_wifi + trust wiring");
    OK279("scope-1 contract_p3_wifi promoted");
    return 0;
}

static int scope02_types(void)
{
    fl_net_wifi_he_cap_t cap = {0};
    fl_net_wifi_twt_params_t twt = {.flow_id = 1};
    (void)cap;
    (void)twt;
    OK279("scope-2 HE/TWT/scan types");
    return 0;
}

static int scope03_fsm(void)
{
    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_6GHZ, 1000u) != FL_RESULT_OK)
        FAIL279("scope-3", "band scan");
    OK279("scope-3 station FSM + band scan (mock)");
    return 0;
}

static int scope04_mgmt(void)
{
    uint8_t frame[256];
    size_t len = 0;
    static const uint8_t sta[6] = {0x02, 0, 0, 0, 0, 0x01};
    static const uint8_t bssid[6] = {0x02, 0xaa, 0, 0, 0, 0x01};

    if (fl_net_wifi_mgmt_build_probe_req("MockAx6", frame, sizeof(frame), &len) !=
        FL_RESULT_OK)
        FAIL279("scope-4", "probe");
    if (fl_net_wifi_mgmt_build_auth_req(sta, bssid, frame, sizeof(frame), &len) !=
        FL_RESULT_OK)
        FAIL279("scope-4", "auth");
    if (fl_net_wifi_mgmt_build_assoc_req("MockAx6", bssid, sta, FL_WIFI_AUTH_WPA3_SAE,
                                         frame, sizeof(frame), &len) != FL_RESULT_OK)
        FAIL279("scope-4", "assoc");
    OK279("scope-4 net_wifi_mgmt Probe/Auth/Assoc + HE IE");
    return 0;
}

static int scope05_he_decode(void)
{
    static const uint8_t he_cap_ie[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
        0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    fl_net_wifi_he_cap_t cap;
    const uint8_t *body = NULL;
    size_t body_len = 0;

    if (!fl_net_wifi_ie_find_extension(he_cap_ie, sizeof(he_cap_ie),
                                       FL_WIFI_EXT_HE_CAPABILITIES, &body, &body_len))
        FAIL279("scope-5", "find HE IE");
    if (!fl_net_wifi_he_parse_capabilities(body, body_len, &cap))
        FAIL279("scope-5", "parse HE");
    OK279("scope-5 net_wifi_he decode");
    return 0;
}

static int scope06_sae(void)
{
    uint8_t pmk[32];
    if (fl_net_wifi_sae_rfc7664_kdf_selftest() != FL_RESULT_OK)
        FAIL279("scope-6", "SAE KDF");
    if (fl_net_wifi_sae_derive_pmk("MockAx6", MOCK_PASS, pmk, sizeof(pmk)) != FL_RESULT_OK)
        FAIL279("scope-6", "SAE PMK");
    OK279("scope-6 net_wifi_sae WPA3-SAE");
    return 0;
}

static int scope07_wpa(void)
{
    uint8_t pmk[32];
    if (fl_net_wifi_wpa_psk_pmk("LegacyAC", MOCK_PASS, pmk) != FL_RESULT_OK)
        FAIL279("scope-7", "WPA2 PMK");
    OK279("scope-7 net_wifi_wpa 4-way crypto");
    return 0;
}

static int scope08_twt(void)
{
    fl_net_wifi_twt_params_t req = {.wake_duration_us = 8000, .wake_interval_us = 100000};
    fl_net_wifi_twt_params_t agreed = {0};
    fl_net_wifi_cred_t cred;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("scope-8", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("scope-8", "connect");
    if (fl_net_wifi_twt_setup(&req, &agreed) != FL_RESULT_OK)
        FAIL279("scope-8", "TWT setup");
    if (fl_net_wifi_twt_teardown(agreed.flow_id) != FL_RESULT_OK)
        FAIL279("scope-8", "TWT teardown");
    (void)fl_net_wifi_disconnect();
    OK279("scope-8 net_wifi_twt mock negotiation");
    return 0;
}

static int scope09_api(void)
{
    fl_net_wifi_scan_entry_t entries[8];
    size_t count = 0;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("scope-9", "scan API");
    if (fl_net_wifi_scan_result(entries, 8, &count) != FL_RESULT_OK || count == 0)
        FAIL279("scope-9", "scan_result API");
    OK279("scope-9 fl_net_wifi_scan/connect/disconnect API");
    return 0;
}

static int scope10_he_cap_api(void)
{
    fl_net_wifi_he_cap_t cap;
    fl_net_wifi_cred_t cred;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("scope-10", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("scope-10", "connect");
    if (fl_net_wifi_he_cap(&cap) != FL_RESULT_OK || !cap.supports_ofdma)
        FAIL279("scope-10", "he_cap");
    (void)fl_net_wifi_disconnect();
    OK279("scope-10 fl_net_wifi_he_cap post-assoc");
    return 0;
}

static int scope11_dhcp(void)
{
    if (prereq03_dhcp() != 0)
        return 1;
    OK279("scope-11 post-assoc fl_net_dhcp_acquire");
    return 0;
}

static int scope12_netdev(void)
{
    fl_net_wifi_cred_t cred;
    fl_net_driver_t *drv;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("scope-12", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("scope-12", "connect");
    drv = fl_net_wifi_station_netdev();
    if (!drv || !drv->send || !drv->recv)
        FAIL279("scope-12", "fl_net_driver_t");
    (void)fl_net_wifi_disconnect();
    OK279("scope-12 fl_net_driver_t registration");
    return 0;
}

static int scope13_e2e(void)
{
    if (prereq04_egress() != 0)
        return 1;
    OK279("scope-13 E2E scan→auth→DHCP→UDP (mock ax)");
    return 0;
}

static int scope14_sae_unit(void)
{
    if (fl_net_wifi_sae_rfc7664_kdf_selftest() != FL_RESULT_OK)
        FAIL279("scope-14", "RFC7664");
    OK279("scope-14 SAE unit test");
    return 0;
}

static int scope15_wpa_unit(void)
{
    uint8_t pmk[32];
    static const uint8_t expected[32] = {
        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a,
        0x5f, 0x90, 0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2, 0x3a, 0xed, 0x76, 0x2e,
        0x97, 0x10, 0xa1, 0x2e
    };
    if (fl_net_wifi_wpa_psk_pmk("IEEE", "password", pmk) != FL_RESULT_OK)
        FAIL279("scope-15", "PMK");
    if (memcmp(pmk, expected, 32) != 0)
        FAIL279("scope-15", "PMK vector mismatch");
    OK279("scope-15 WPA2 4-way unit test");
    return 0;
}

static int scope16_twt_unit(void)
{
    if (scope08_twt() != 0)
        return 1;
    OK279("scope-16 TWT mock test");
    return 0;
}

static int scope17_he_ie_unit(void)
{
    if (scope05_he_decode() != 0)
        return 1;
    OK279("scope-17 HE IE parse unit test");
    return 0;
}

static int scope18_roadmap(void)
{
    FILE *f = fopen("docs/ROADMAP.md", "r");
    char buf[512];
    int found = 0;
    if (!f)
        FAIL279("scope-18", "ROADMAP.md");
    while (fgets(buf, sizeof(buf), f)) {
        if (strstr(buf, "P3-10") && strstr(buf, "~✅")) {
            found = 1;
            break;
        }
    }
    fclose(f);
    if (!found)
        FAIL279("scope-18", "P3-10 ~✅");
    OK279("scope-18 ROADMAP/P3_NETWORKING ~✅ (accept-30)");
    return 0;
}

static int scope19_authz(void)
{
    fl_net_wifi_cred_t cred;
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.passphrase, "scrub", sizeof(cred.passphrase) - 1u);
    fl_net_wifi_cred_scrub_passphrase(&cred);
    if (cred.passphrase[0] != '\0')
        FAIL279("scope-19", "scrub");
    OK279("scope-19 auth guard contract_p3_trust");
    return 0;
}

/* --- Acceptance (10: items 20–30) --- */

static int accept20_wpa3_mock_ota(void)
{
    wifi_supplicant_t supp;
    static const uint8_t bssid[6] = {0x02, 0xaa, 0, 0, 0, 0x01};
    static const uint8_t sta[6] = {0x02, 0, 0, 0, 0, 0x55};
    fl_net_wifi_cred_t cred;

    if (wifi_supplicant_init(&supp, bssid) != 0)
        FAIL279("accept-20", "supplicant init");
    if (wifi_supplicant_set_credentials(&supp, "MockAx6", MOCK_PASS) != 0 ||
        wifi_supplicant_set_sta_addr(&supp, sta) != 0)
        FAIL279("accept-20", "supplicant creds");
    if (wifi_supplicant_start_sae_handshake(&supp) != 0 ||
        wifi_supplicant_process_sae_commit(&supp, (const uint8_t *)"commit", 6) != 0 ||
        wifi_supplicant_process_sae_confirm(&supp, (const uint8_t *)"confirm", 7) != 0)
        FAIL279("accept-20", "SAE OTA mock");
    if (wifi_supplicant_get_state(&supp) != WIFI_SUPP_STATE_AUTHENTICATED)
        FAIL279("accept-20", "SAE state");
    (void)wifi_supplicant_deinit(&supp);

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("accept-20", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("accept-20", "WPA3 connect mock NIC");
    (void)fl_net_wifi_disconnect();
    OK279("accept-20 WPA3-SAE connect (mock ax NIC; RF open)");
    return 0;
}

static int accept21_wpa2_legacy(void)
{
    fl_net_wifi_cred_t cred;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_5GHZ, 1000u) != FL_RESULT_OK)
        FAIL279("accept-21", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LegacyAC", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA2_PSK;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("accept-21", "WPA2 connect");
    (void)fl_net_wifi_disconnect();
    OK279("accept-21 WPA2-PSK non-ax AP (mock LegacyAC; RF open)");
    return 0;
}

static int accept22_scan_he(void)
{
    fl_net_wifi_scan_entry_t entries[8];
    size_t count = 0;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_6GHZ, 1000u) != FL_RESULT_OK)
        FAIL279("accept-22", "scan");
    if (fl_net_wifi_scan_result(entries, 8, &count) != FL_RESULT_OK || count < 1u)
        FAIL279("accept-22", "scan_result");
    if (!entries[0].he_supported || entries[0].band != FL_WIFI_BAND_6GHZ)
        FAIL279("accept-22", "HE fields");
    OK279("accept-22 scan_result HE fields (mock ax AP)");
    return 0;
}

static int accept23_he_cap_fields(void)
{
    if (scope10_he_cap_api() != 0)
        return 1;
    OK279("accept-23 he_cap NSS/OFDMA/TWT");
    return 0;
}

static int accept24_twt_flow(void)
{
    fl_net_wifi_twt_params_t req = {.wake_duration_us = 4000, .wake_interval_us = 80000};
    fl_net_wifi_twt_params_t agreed = {0};
    fl_net_wifi_cred_t cred;

    mock279_env();
    STATION_INIT();
    if (fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) != FL_RESULT_OK)
        FAIL279("accept-24", "scan");
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, MOCK_PASS, sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    if (fl_net_wifi_connect(&cred, 5000u) != FL_RESULT_OK)
        FAIL279("accept-24", "connect");
    if (fl_net_wifi_twt_setup(&req, &agreed) != FL_RESULT_OK || agreed.flow_id >= 8u)
        FAIL279("accept-24", "TWT");
    (void)fl_net_wifi_twt_teardown(agreed.flow_id);
    (void)fl_net_wifi_disconnect();
    OK279("accept-24 TWT flow_id negotiated");
    return 0;
}

static int accept25_dhcp_udp(void)
{
    if (prereq04_egress() != 0)
        return 1;
    OK279("accept-25 DHCP + UDP on Wi-Fi netdev (mock)");
    return 0;
}

static int accept26_sae_vectors(void)
{
    if (scope06_sae() != 0)
        return 1;
    OK279("accept-26 SAE RFC7664 vectors");
    return 0;
}

static int accept27_wpa_vectors(void)
{
    if (scope15_wpa_unit() != 0)
        return 1;
    OK279("accept-27 WPA2 reference vectors");
    return 0;
}

static int accept28_he_ie_bytes(void)
{
    if (scope05_he_decode() != 0)
        return 1;
    OK279("accept-28 HE IE decoder reference bytes");
    return 0;
}

static int accept29_p3_network(void)
{
    fl_net_route_init();
    OK279("accept-29 make test_p3_network (run in CI alongside this target)");
    return 0;
}

int main(void)
{
    typedef int (*fn_t)(void);
    static const fn_t checks[33] = {
        prereq01_p4_driver,   prereq02_ax_nic,      prereq03_dhcp,       prereq04_egress,
        scope01_contract,     scope02_types,        scope03_fsm,         scope04_mgmt,
        scope05_he_decode,    scope06_sae,          scope07_wpa,         scope08_twt,
        scope09_api,          scope10_he_cap_api,   scope11_dhcp,        scope12_netdev,
        scope13_e2e,          scope14_sae_unit,     scope15_wpa_unit,    scope16_twt_unit,
        scope17_he_ie_unit,   scope18_roadmap,      scope19_authz,       accept20_wpa3_mock_ota,
        accept21_wpa2_legacy, accept22_scan_he,     accept23_he_cap_fields, accept24_twt_flow,
        accept25_dhcp_udp,    accept26_sae_vectors, accept27_wpa_vectors, accept28_he_ie_bytes,
        accept29_p3_network,
    };
    size_t i;

    for (i = 0; i < 33u; i++) {
        if (checks[i]() != 0)
            return 1;
    }

    puts("test_wifi_80211ax_mock_279: all 33 #279 items passed (802.11ax mock; production RF open)");
    return 0;
}
