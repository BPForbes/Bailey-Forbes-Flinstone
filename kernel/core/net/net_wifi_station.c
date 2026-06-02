#include "net_wifi_station.h"

#include "net_wifi_he.h"

#include <string.h>

#define FL_NET_WIFI_HOSTED_LAB 1

static fl_net_wifi_state_t s_wifi_state = FL_WIFI_STATE_IDLE;
static fl_net_wifi_he_cap_t s_negotiated_he;

#if defined(FL_NET_WIFI_HOSTED_LAB)
static fl_net_wifi_scan_entry_t s_lab_scan[8];
static size_t s_lab_scan_count;
static char s_lab_joined_ssid[FL_WIFI_SSID_MAX];
#endif

#if defined(FL_NET_WIFI_HOSTED_LAB)
static void lab_seed_scan(uint8_t band) {
    s_lab_scan_count = 0;
    memset(s_lab_scan, 0, sizeof(s_lab_scan));
    if (band == FL_WIFI_BAND_6GHZ)
        return;
    {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];
        strncpy(e->ssid, "LabAxHome", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0x02;
        e->bssid[1] = 0x11;
        e->bssid[2] = 0x22;
        e->bssid[3] = 0x33;
        e->bssid[4] = 0x44;
        e->bssid[5] = 0x55;
        e->rssi_dbm = -42;
        e->channel = 36;
        e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
        e->band = FL_WIFI_BAND_5GHZ;
        e->channel_width_mhz = 80;
        e->he_supported = 1;
        e->bss_color = 5;
        e->twt_responder = 1;
    }
    if (band == FL_WIFI_BAND_2GHZ || band == FL_WIFI_BAND_ANY) {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];
        strncpy(e->ssid, "GuestOpen", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0xaa;
        e->bssid[1] = 0xbb;
        e->bssid[2] = 0xcc;
        e->bssid[3] = 0xdd;
        e->bssid[4] = 0xee;
        e->bssid[5] = 0xff;
        e->rssi_dbm = -65;
        e->channel = 6;
        e->auth_mode = FL_WIFI_AUTH_OPEN;
        e->band = FL_WIFI_BAND_2GHZ;
        e->channel_width_mhz = 20;
    }
}
#endif

fl_result_t fl_net_wifi_station_init(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_wifi_state = FL_WIFI_STATE_IDLE;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_scan_count = 0;
    s_lab_joined_ssid[0] = '\0';
#endif
    return FL_RESULT_OK;
}

fl_net_driver_t *fl_net_wifi_station_netdev(void) {
    return NULL;
}

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms) {
    (void)timeout_ms;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    lab_seed_scan(band);
    s_wifi_state = FL_WIFI_STATE_SCANNING;
    return FL_RESULT_OK;
#else
    (void)band;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                    size_t *count_out) {
    size_t i;

    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    *count_out = 0;
    for (i = 0; i < s_lab_scan_count && i < cap; i++) {
        entries[i] = s_lab_scan[i];
        (*count_out)++;
    }
    s_wifi_state = FL_WIFI_STATE_IDLE;
    return FL_RESULT_OK;
#else
    *count_out = 0u;
    return FL_RESULT_NOSYS;
#endif
}

#if defined(FL_NET_WIFI_HOSTED_LAB)
static const fl_net_wifi_scan_entry_t *lab_find_ssid(const char *ssid) {
    size_t i;
    if (!ssid)
        return NULL;
    for (i = 0; i < s_lab_scan_count; i++) {
        if (!strcmp(s_lab_scan[i].ssid, ssid))
            return &s_lab_scan[i];
    }
    return NULL;
}
#endif

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;
    (void)timeout_ms;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    {
        const fl_net_wifi_scan_entry_t *ap = lab_find_ssid(cred->ssid);
        if (!ap) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_NOENT;
        }
        if (ap->auth_mode != FL_WIFI_AUTH_OPEN &&
            (cred->passphrase[0] == '\0')) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_INVAL;
        }
        memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
        s_negotiated_he.supports_ofdma = 1;
        s_negotiated_he.supports_twt = ap->twt_responder;
        s_negotiated_he.bss_color = ap->bss_color;
        s_negotiated_he.channel_width_mhz = ap->channel_width_mhz;
        s_negotiated_he.max_nss_rx = 2;
        s_negotiated_he.max_nss_tx = 2;
        strncpy(s_lab_joined_ssid, cred->ssid, sizeof(s_lab_joined_ssid) - 1u);
        s_wifi_state = FL_WIFI_STATE_CONNECTED;
        return FL_RESULT_OK;
    }
#else
    (void)cred;
    s_wifi_state = FL_WIFI_STATE_ERROR;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_disconnect(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_joined_ssid[0] = '\0';
#endif
    s_wifi_state = FL_WIFI_STATE_IDLE;
    return FL_RESULT_OK;
}

fl_net_wifi_state_t fl_net_wifi_state(void) {
    return s_wifi_state;
}

fl_result_t fl_net_wifi_he_cap(fl_net_wifi_he_cap_t *cap_out) {
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;
    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_twt_setup(const fl_net_wifi_twt_params_t *req,
                                  fl_net_wifi_twt_params_t *agreed_out) {
    (void)req;
    (void)agreed_out;
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_twt_teardown(uint8_t flow_id) {
    (void)flow_id;
    return FL_RESULT_NOSYS;
}
