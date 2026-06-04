#include "net_wifi_station.h"

#include "net_iface.h"
#include "net_netdev.h"
#include "net_wifi_netdev.h"
#include "net_route.h"
#include "net_wifi_crypto.h"
#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_twt.h"
#include "net_wifi_wpa.h"
#include "net_wire.h"

#include <string.h>

#define FL_NET_WIFI_HOSTED_LAB 1

static fl_net_wifi_state_t s_wifi_state = FL_WIFI_STATE_IDLE;
static fl_net_wifi_he_cap_t s_negotiated_he;
static int s_netdev_ready;

#if defined(FL_NET_WIFI_HOSTED_LAB)
static fl_net_wifi_scan_entry_t s_lab_scan[8];
static size_t s_lab_scan_count;
static char s_lab_joined_ssid[FL_WIFI_SSID_MAX];
#endif

void fl_net_wifi_cred_scrub_passphrase(fl_net_wifi_cred_t *cred) {
    if (cred)
        fl_net_wifi_crypto_memzero(cred->passphrase, sizeof(cred->passphrase));
}

#if defined(FL_NET_WIFI_HOSTED_LAB)
static const uint8_t s_lab_probe_resp_ies[] = {
    FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
    0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
};

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
        (void)fl_net_wifi_scan_enrich_from_ies(s_lab_probe_resp_ies,
                                               sizeof(s_lab_probe_resp_ies), e);
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
    s_netdev_ready = 0;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_scan_count = 0;
    s_lab_joined_ssid[0] = '\0';
    fl_net_wifi_wpa_lab_reset();
    fl_net_wifi_twt_lab_reset();
#endif
    return FL_RESULT_OK;
}

fl_net_driver_t *fl_net_wifi_station_netdev(void) {
#if defined(FL_NET_WIFI_HOSTED_LAB)
    if (s_wifi_state == FL_WIFI_STATE_UP || s_wifi_state == FL_WIFI_STATE_DHCP ||
        s_wifi_state == FL_WIFI_STATE_CONNECTED) {
        {
            fl_net_driver_t *wlan = fl_net_wifi_netdev_driver();
            if (wlan)
                return wlan;
        }
    }
#endif
    return NULL;
}

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms) {
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

static fl_result_t lab_derive_pmk(const fl_net_wifi_cred_t *cred, uint8_t pmk[FL_NET_WIFI_PMK_LEN]) {
    if (cred->auth_mode == FL_WIFI_AUTH_OPEN || cred->auth_mode == FL_WIFI_AUTH_OWE)
        return FL_RESULT_OK;
    if (cred->auth_mode == FL_WIFI_AUTH_WPA3_SAE)
        return fl_net_wifi_sae_derive_pmk(cred->ssid, cred->passphrase, pmk,
                                          FL_NET_WIFI_PMK_LEN);
    if (cred->auth_mode == FL_WIFI_AUTH_WPA2_PSK)
        return fl_net_wifi_wpa_psk_pmk(cred->ssid, cred->passphrase, pmk);
    return FL_RESULT_NOSYS;
}
#endif

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;
#if defined(FL_NET_WIFI_HOSTED_LAB)
#endif
    (void)timeout_ms;
#if defined(FL_NET_WIFI_HOSTED_LAB)
    {
        const fl_net_wifi_scan_entry_t *ap = lab_find_ssid(cred->ssid);
        uint8_t pmk[FL_NET_WIFI_PMK_LEN];
        uint8_t probe[128];
        uint8_t assoc[160];
        size_t frame_len = 0;
        uint8_t sta_mac[6];
        fl_result_t rc;
        fl_net_driver_t *drv;

        if (!ap) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_NOENT;
        }
        if (ap->auth_mode != FL_WIFI_AUTH_OPEN && cred->passphrase[0] == '\0') {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_INVAL;
        }

        s_wifi_state = FL_WIFI_STATE_AUTHING;
        memset(pmk, 0, sizeof(pmk));
        rc = lab_derive_pmk(cred, pmk);
        if (rc != FL_RESULT_OK) {
            fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return rc;
        }

        if (fl_net_wifi_mgmt_build_probe_req(cred->ssid, probe, sizeof(probe), &frame_len) !=
            FL_RESULT_OK) {
            fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_ERR;
        }

        s_wifi_state = FL_WIFI_STATE_ASSOC;
        fl_net_loopback_mac_host(sta_mac);
        if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap->bssid, sta_mac, assoc,
                                             sizeof(assoc), &frame_len) != FL_RESULT_OK) {
            fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return FL_RESULT_ERR;
        }

        if (ap->auth_mode != FL_WIFI_AUTH_OPEN) {
            rc = fl_net_wifi_wpa4_install_ptk(pmk, sizeof(pmk));
            fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
            if (rc != FL_RESULT_OK) {
                s_wifi_state = FL_WIFI_STATE_ERROR;
                return rc;
            }
        } else {
            fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
        }

        memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
        s_negotiated_he.supports_ofdma = 1;
        s_negotiated_he.supports_mu_mimo = 1;
        s_negotiated_he.supports_twt = ap->twt_responder;
        s_negotiated_he.bss_color = ap->bss_color;
        s_negotiated_he.channel_width_mhz = ap->channel_width_mhz;
        s_negotiated_he.max_nss_rx = 2;
        s_negotiated_he.max_nss_tx = 2;
        if (ap->he_supported)
            s_negotiated_he.supports_6ghz = 0;

        strncpy(s_lab_joined_ssid, cred->ssid, sizeof(s_lab_joined_ssid) - 1u);
        s_wifi_state = FL_WIFI_STATE_CONNECTED;

        s_wifi_state = FL_WIFI_STATE_DHCP;
        fl_net_loopback_mac_host(sta_mac);
        rc = fl_net_wifi_netdev_up(ap, sta_mac);
        if (rc != FL_RESULT_OK) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return rc;
        }
        (void)drv;
        s_wifi_state = FL_WIFI_STATE_UP;
        fl_net_iface_refresh();
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
    fl_net_wifi_wpa_lab_reset();
    fl_net_wifi_twt_lab_reset();
    fl_net_wifi_netdev_down();
    fl_net_iface_refresh();
#endif
    s_wifi_state = FL_WIFI_STATE_IDLE;
    return FL_RESULT_OK;
}

fl_net_wifi_state_t fl_net_wifi_state(void) {
    return s_wifi_state;
}

fl_result_t fl_net_wifi_twt_setup(const fl_net_wifi_twt_params_t *req,
                                  fl_net_wifi_twt_params_t *agreed_out) {
#if defined(FL_NET_WIFI_HOSTED_LAB)
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;
    return fl_net_wifi_twt_negotiate(req, agreed_out);
#else
    (void)req;
    (void)agreed_out;
    return FL_RESULT_NOSYS;
#endif
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
