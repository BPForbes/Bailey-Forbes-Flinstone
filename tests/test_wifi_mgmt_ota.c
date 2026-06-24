/**
 * Unit tests for Wi-Fi management-frame OTA builders/parsers (#328 Task 2).
 */
#include "net_wifi_mgmt.h"
#include "net_wifi_he.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static const uint8_t k_sta[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t k_bssid[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
static const uint8_t k_bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static int test_probe_req_addrs(void) {
    uint8_t frame[128];
    size_t len = 0;

    ASSERT(fl_net_wifi_mgmt_build_probe_req("HiddenNet", k_sta, frame, sizeof(frame), &len) ==
           FL_RESULT_OK);
    ASSERT(memcmp(frame + 4, k_bcast, 6) == 0);
    ASSERT(memcmp(frame + 10, k_sta, 6) == 0);
    ASSERT(memcmp(frame + 16, k_bcast, 6) == 0);
    ASSERT(frame[0] == 0x40u);
    return 0;
}

static int test_scan_ssid_validate(void) {
    size_t slen = 0;
    char long_ssid[40];

    ASSERT(fl_net_wifi_scan_ssid_validate("LabAxHome", &slen) == FL_RESULT_OK);
    ASSERT(slen == 9u);
    ASSERT(fl_net_wifi_scan_ssid_validate(NULL, NULL) == FL_RESULT_INVAL);
    memset(long_ssid, 'A', sizeof(long_ssid));
    long_ssid[33] = '\0';
    ASSERT(fl_net_wifi_scan_ssid_validate(long_ssid, NULL) == FL_RESULT_INVAL);
    return 0;
}

static uint8_t *append_ie(uint8_t *p, uint8_t eid, const void *data, size_t dlen) {
    *p++ = eid;
    *p++ = (uint8_t)dlen;
    memcpy(p, data, dlen);
    return p + dlen;
}

static int test_parse_probe_resp(void) {
    static const uint8_t he_cap[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
        0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t he_op[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
    };
    static const uint8_t rsne[] = {
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x08
    };
    uint8_t frame[256];
    uint8_t *ies;
    fl_net_wifi_mgmt_bss_info_t bss;
    const char ssid[] = "LabAxHome";

    memset(frame, 0, sizeof(frame));
    frame[0] = 0x50u;
    memcpy(frame + 4, k_bssid, 6);
    memcpy(frame + 10, k_bssid, 6);
    memcpy(frame + 16, k_sta, 6);
    frame[34] = 0x11u;
    frame[35] = 0x00u;
    ies = frame + FL_WIFI_MGMT_HDR_LEN + 12u;
    ies = append_ie(ies, FL_WIFI_ELEM_SSID, ssid, strlen(ssid));
    ies = append_ie(ies, FL_WIFI_ELEM_SUPPORTED_RATES, "\x8c\x12\x98\x24\xb0\x48\x60\x6c", 8);
    ies = append_ie(ies, FL_WIFI_ELEM_DS_PARAM, "\x36", 1);
    ies = append_ie(ies, FL_WIFI_ELEM_RSN, rsne, sizeof(rsne));
    memcpy(ies, he_cap, sizeof(he_cap));
    ies += sizeof(he_cap);
    memcpy(ies, he_op, sizeof(he_op));
    ies += sizeof(he_op);

    ASSERT(fl_net_wifi_mgmt_parse_probe_resp(frame, (size_t)(ies - frame), &bss) ==
           FL_RESULT_OK);
    ASSERT(strcmp(bss.ssid, "LabAxHome") == 0);
    ASSERT(bss.channel == 54u);
    ASSERT(bss.auth_mode == FL_WIFI_AUTH_WPA3_SAE);
    ASSERT(bss.he_supported == 1u);
    ASSERT(bss.bss_color == 5u);
    ASSERT(bss.channel_width_mhz == 160u);
    return 0;
}

static int test_parse_beacon(void) {
    uint8_t frame[200];
    uint8_t *ies;
    fl_net_wifi_mgmt_bss_info_t bss;
    const char ssid[] = "BeaconNet";

    memset(frame, 0, sizeof(frame));
    frame[0] = 0x80u;
    memcpy(frame + 4, k_bssid, 6);
    memcpy(frame + 10, k_bssid, 6);
    memcpy(frame + 16, k_bssid, 6);
    frame[32] = 0x64u;
    frame[33] = 0x00u;
    ies = frame + FL_WIFI_MGMT_HDR_LEN + 12u;
    ies = append_ie(ies, FL_WIFI_ELEM_SSID, ssid, strlen(ssid));
    ies = append_ie(ies, FL_WIFI_ELEM_DS_PARAM, "\x11", 1);

    ASSERT(fl_net_wifi_mgmt_parse_beacon(frame, (size_t)(ies - frame), &bss) == FL_RESULT_OK);
    ASSERT(strcmp(bss.ssid, "BeaconNet") == 0);
    ASSERT(bss.beacon_interval == 100u);
    ASSERT(bss.channel == 17u);
    return 0;
}

static int test_parse_auth_resp(void) {
    uint8_t frame[128];
    size_t len = 0;
    uint16_t alg = 0;
    uint16_t seq = 0;
    uint16_t status = 0xffffu;

    ASSERT(fl_net_wifi_mgmt_build_auth_resp(k_bssid, k_sta, 2u, frame, sizeof(frame), &len) ==
           FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_auth_resp(frame, len, &alg, &seq, &status, NULL, NULL) ==
           FL_RESULT_OK);
    ASSERT(alg == FL_WIFI_AUTH_ALG_OPEN);
    ASSERT(seq == 2u);
    ASSERT(status == 0u);
    return 0;
}

static int test_parse_assoc_resp(void) {
    uint8_t frame[256];
    size_t len = 0;
    uint16_t status = 0xffffu;
    uint16_t aid = 0;
    fl_net_wifi_he_cap_t he;

    ASSERT(fl_net_wifi_mgmt_build_assoc_resp(k_bssid, k_sta, frame, sizeof(frame), &len) ==
           FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_assoc_resp(frame, len, &status, &aid, &he) == FL_RESULT_OK);
    ASSERT(status == 0u);
    ASSERT(aid == 1u);
    ASSERT(he.bss_color == 5u);
    return 0;
}

static int test_assoc_req_he_from_sta(void) {
    fl_net_wifi_he_cap_t sta = {.channel_width_mhz = 80u,
                                .supports_ofdma = 1u,
                                .supports_twt = 1u};
    uint8_t frame[256];
    size_t len = 0;
    const uint8_t *ies = NULL;
    size_t ies_len = 0;
    const uint8_t *he_body = NULL;
    size_t he_body_len = 0;
    fl_net_wifi_he_cap_t parsed;

    ASSERT(fl_net_wifi_mgmt_build_assoc_req("LabAxHome", k_bssid, k_sta, FL_WIFI_AUTH_WPA3_SAE,
                                            &sta, frame, sizeof(frame), &len) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_mgmt_ies(frame, len, &ies, &ies_len) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_CAPABILITIES, &he_body,
                                         &he_body_len));
    ASSERT(fl_net_wifi_he_parse_capabilities(he_body, he_body_len, &parsed));
    ASSERT(parsed.channel_width_mhz == 80u);
    return 0;
}

static int test_scan_entry_merge(void) {
    fl_net_wifi_scan_entry_t a;
    fl_net_wifi_scan_entry_t b;

    memset(&a, 0, sizeof(a));
    memcpy(a.bssid, k_bssid, 6);
    strncpy(a.ssid, "Merged", sizeof(a.ssid) - 1u);
    a.he_supported = 0;

    memset(&b, 0, sizeof(b));
    memcpy(b.bssid, k_bssid, 6);
    b.he_supported = 1u;
    b.bss_color = 7u;
    b.channel_width_mhz = 160u;
    b.twt_responder = 1u;

    fl_net_wifi_scan_entry_merge(&a, &b);
    ASSERT(a.he_supported == 1u);
    ASSERT(a.bss_color == 7u);
    ASSERT(a.channel_width_mhz == 160u);
    ASSERT(a.twt_responder == 1u);
    ASSERT(strcmp(a.ssid, "Merged") == 0);
    return 0;
}

static int test_sae_null_body_reject(void) {
    uint8_t frame[64];
    size_t len = 0;

    ASSERT(fl_net_wifi_mgmt_build_sae_auth(k_sta, k_bssid, 1u, NULL, 4u, frame, sizeof(frame),
                                           &len) == FL_RESULT_INVAL);
    return 0;
}

static int test_assoc_req_fixed_fields(void) {
    uint8_t frame[256];
    size_t len = 0;
    const uint8_t *ies = NULL;
    size_t ies_len = 0;

    ASSERT(fl_net_wifi_mgmt_build_assoc_req("LabAxHome", k_bssid, k_sta, FL_WIFI_AUTH_OPEN, NULL,
                                            frame, sizeof(frame), &len) == FL_RESULT_OK);
    ASSERT(frame[24] == 0x00u);
    ASSERT(frame[25] == 0x00u);
    ASSERT(frame[26] == 0x0au);
    ASSERT(frame[27] == 0x00u);
    ASSERT(frame[28] == FL_WIFI_ELEM_SSID);
    ASSERT(fl_net_wifi_mgmt_parse_mgmt_ies(frame, len, &ies, &ies_len) == FL_RESULT_OK);
    ASSERT(ies_len > 0u);
    return 0;
}

static int test_sae_auth_builders(void) {
    static const uint8_t k_commit[] = "commit";
    static const uint8_t k_confirm[] = "confirm";
    uint8_t frame[128];
    size_t len = 0;
    uint16_t alg = 0;
    uint16_t seq = 0;

    ASSERT(fl_net_wifi_mgmt_build_auth_sae_commit(k_sta, k_bssid, k_commit, sizeof(k_commit) - 1u,
                                                   frame, sizeof(frame), &len) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_auth_resp(frame, len, &alg, &seq, NULL, NULL, NULL) ==
           FL_RESULT_OK);
    ASSERT(alg == FL_WIFI_AUTH_ALG_SAE);
    ASSERT(seq == 1u);

    ASSERT(fl_net_wifi_mgmt_build_auth_sae_confirm(k_sta, k_bssid, k_confirm,
                                                   sizeof(k_confirm) - 1u, frame, sizeof(frame),
                                                   &len) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_mgmt_parse_auth_resp(frame, len, &alg, &seq, NULL, NULL, NULL) ==
           FL_RESULT_OK);
    ASSERT(seq == 2u);
    return 0;
}

int main(void) {
    if (test_probe_req_addrs() != 0)
        return 1;
    if (test_scan_ssid_validate() != 0)
        return 1;
    if (test_parse_probe_resp() != 0)
        return 1;
    if (test_parse_beacon() != 0)
        return 1;
    if (test_parse_auth_resp() != 0)
        return 1;
    if (test_parse_assoc_resp() != 0)
        return 1;
    if (test_assoc_req_he_from_sta() != 0)
        return 1;
    if (test_scan_entry_merge() != 0)
        return 1;
    if (test_sae_null_body_reject() != 0)
        return 1;
    if (test_assoc_req_fixed_fields() != 0)
        return 1;
    if (test_sae_auth_builders() != 0)
        return 1;
    puts("test_wifi_mgmt_ota: all passed");
    return 0;
}
