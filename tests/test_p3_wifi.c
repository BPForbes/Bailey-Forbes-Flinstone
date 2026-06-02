/**
 * P3-10 Wi-Fi (#279) foundation tests — HE IE parser and station API stubs.
 */
#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_station.h"

#include <stdio.h>
#include <string.h>

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

static int test_he_operation_bss_color(void) {
    static const uint8_t he_op_ie[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION,
        0x02, 0x00, 0x00, 0x15
    };
    const uint8_t *body = NULL;
    size_t body_len = 0;
    uint8_t color = 0;
    uint8_t twt = 0;

    ASSERT(fl_net_wifi_ie_find_extension(he_op_ie, sizeof(he_op_ie),
                                         FL_WIFI_EXT_HE_OPERATION, &body,
                                         &body_len));
    ASSERT(fl_net_wifi_he_parse_operation(body, body_len, &color, &twt));
    ASSERT(color == 21u);
    ASSERT(twt == 1u);
    return 0;
}

static int test_scan_enrich_from_ies(void) {
    static const uint8_t ies[] = {
        FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
        0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
        0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION,
        0x02, 0x00, 0x00, 0x05
    };
    fl_net_wifi_scan_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    ASSERT(fl_net_wifi_scan_enrich_from_ies(ies, sizeof(ies), &entry));
    ASSERT(entry.he_supported == 1u);
    ASSERT(entry.bss_color == 5u);
    ASSERT(entry.channel_width_mhz == 160u);
    return 0;
}

static int test_station_api_nosys(void) {
    fl_net_wifi_scan_entry_t entries[4];
    size_t count = 0;

    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_state() == FL_WIFI_STATE_IDLE);
    ASSERT(fl_net_wifi_station_netdev() == NULL);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_NOSYS);
    ASSERT(fl_net_wifi_scan_result(entries, 4, &count) == FL_RESULT_NOSYS);
    {
        fl_net_wifi_cred_t cred;
        memset(&cred, 0, sizeof(cred));
        ASSERT(fl_net_wifi_connect(&cred, 0u) == FL_RESULT_NOSYS);
    }
    ASSERT(fl_net_wifi_twt_setup(NULL, NULL) == FL_RESULT_NOSYS);
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}

static int test_mgmt_hdr_probe(void) {
    uint8_t probe[24] = {0x40, 0x00};
    ASSERT(fl_net_wifi_mgmt_hdr_valid(probe, sizeof(probe)));
    probe[0] = 0x08;
    ASSERT(!fl_net_wifi_mgmt_hdr_valid(probe, sizeof(probe)));
    return 0;
}

int main(void) {
    if (test_he_capabilities_parse() != 0)
        return 1;
    if (test_he_operation_bss_color() != 0)
        return 1;
    if (test_scan_enrich_from_ies() != 0)
        return 1;
    if (test_station_api_nosys() != 0)
        return 1;
    if (test_mgmt_hdr_probe() != 0)
        return 1;
    puts("test_p3_wifi: all passed");
    return 0;
}
