/*
 * Lab virtual WiFi driver — scan cache, supplicant, mgmt/auth/assoc, PTK.
 * Execution layer for hosted simulation; net_wifi_station orchestrates only.
 */

#include "wifi_lab_backend.h"

#include "wifi_supplicant.h"

#include "net_wifi_he.h"
#include "net_wifi_crypto.h"
#include "net_wire.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_wpa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fl_net_wifi_scan_entry_t s_lab_scan[8];
static size_t s_lab_scan_count;
static fl_net_wifi_he_cap_t s_negotiated_he;
static int s_lab_connected;

static const uint8_t s_lab_probe_resp_ies[] = {
    FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
    0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
};

/* 802.11ax on 2.4 GHz — HE present without 80/160 MHz PHY widths. */
static const uint8_t s_lab_ax_ies_2g[] = {
    FL_WIFI_ELEM_ID_EXTENSION, 4u, FL_WIFI_EXT_HE_CAPABILITIES, 0x00, 0x80, 0x00,
    FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x07
};

static void lab_apply_ax_ap(fl_net_wifi_scan_entry_t *e)
{
    const uint8_t *ies;
    size_t ies_len;

    if (!e)
        return;
    if (e->band == FL_WIFI_BAND_2GHZ) {
        ies = s_lab_ax_ies_2g;
        ies_len = sizeof(s_lab_ax_ies_2g);
    } else {
        ies = s_lab_probe_resp_ies;
        ies_len = sizeof(s_lab_probe_resp_ies);
    }
    (void)fl_net_wifi_scan_enrich_from_ies(ies, ies_len, e);
    if (e->band == FL_WIFI_BAND_2GHZ) {
        if (e->channel_width_mhz == 0u || e->channel_width_mhz > 40u)
            e->channel_width_mhz = 20u;
    }
}

static void lab_seed_scan(uint8_t band)
{
    s_lab_scan_count = 0;
    memset(s_lab_scan, 0, sizeof(s_lab_scan));

    if (band == FL_WIFI_BAND_6GHZ) {
        fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

        strncpy(e->ssid, "LabAx6", sizeof(e->ssid) - 1u);
        e->bssid[0] = 0x02;
        e->bssid[1] = 0x66;
        e->bssid[2] = 0x00;
        e->bssid[3] = 0x00;
        e->bssid[4] = 0x00;
        e->bssid[5] = 0x01;
        e->rssi_dbm = -48;
        e->channel = 37;
        e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
        e->band = FL_WIFI_BAND_6GHZ;
        e->channel_width_mhz = 160;
        lab_apply_ax_ap(e);
        return;
    }

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
        lab_apply_ax_ap(e);
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
        lab_apply_ax_ap(e);
    }
    {
        const char *home_ssid = getenv("FL_NET_WIFI_HOME_SSID");

        if (home_ssid && home_ssid[0] && s_lab_scan_count < 8u) {
            const char *home_auth = getenv("FL_NET_WIFI_HOME_AUTH");
            const char *home_band_env = getenv("FL_NET_WIFI_HOME_BAND");
            uint8_t home_band;
            uint8_t home_channel;

            if (home_band_env &&
                (!strcmp(home_band_env, "5") || !strcmp(home_band_env, "5ghz"))) {
                home_band = FL_WIFI_BAND_5GHZ;
                home_channel = 36;
            } else if (home_band_env &&
                       (!strcmp(home_band_env, "6") || !strcmp(home_band_env, "6ghz"))) {
                home_band = FL_WIFI_BAND_6GHZ;
                home_channel = 37;
            } else {
                home_band = FL_WIFI_BAND_2GHZ;
                home_channel = 6;
            }
            if (band == FL_WIFI_BAND_ANY || band == home_band) {
                fl_net_wifi_scan_entry_t *e = &s_lab_scan[s_lab_scan_count++];

                memset(e, 0, sizeof(*e));
                strncpy(e->ssid, home_ssid, sizeof(e->ssid) - 1u);
                e->bssid[0] = 0x02;
                e->bssid[5] = 0xfe;
                e->rssi_dbm = -55;
                e->channel = home_channel;
                e->channel_width_mhz = 20;
                e->band = home_band;
                if (home_auth &&
                    (!strcmp(home_auth, "wpa3") || !strcmp(home_auth, "sae")))
                    e->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
                else if (home_auth && !strcmp(home_auth, "open"))
                    e->auth_mode = FL_WIFI_AUTH_OPEN;
                else
                    e->auth_mode = FL_WIFI_AUTH_WPA2_PSK;
                lab_apply_ax_ap(e);
            }
        }
    }
}

static const fl_net_wifi_scan_entry_t *lab_find_ssid(const char *ssid)
{
    size_t i;

    if (!ssid)
        return NULL;
    for (i = 0; i < s_lab_scan_count; i++) {
        if (!strcmp(s_lab_scan[i].ssid, ssid))
            return &s_lab_scan[i];
    }
    return NULL;
}

static fl_result_t lab_derive_pmk(const fl_net_wifi_cred_t *cred,
				  uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    if (cred->auth_mode == FL_WIFI_AUTH_OPEN || cred->auth_mode == FL_WIFI_AUTH_OWE)
        return FL_RESULT_OK;
    if (cred->auth_mode == FL_WIFI_AUTH_WPA3_SAE)
        return fl_net_wifi_sae_derive_pmk(cred->ssid, cred->passphrase, pmk,
                                          FL_NET_WIFI_PMK_LEN);
    if (cred->auth_mode == FL_WIFI_AUTH_WPA2_PSK)
        return fl_net_wifi_wpa_psk_pmk(cred->ssid, cred->passphrase, pmk);
    return FL_RESULT_NOSYS;
}

static fl_result_t lab_supplicant_auth(const fl_net_wifi_cred_t *cred,
				       const fl_net_wifi_scan_entry_t *ap,
				       const uint8_t sta_mac[6],
				       uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    wifi_supplicant_t supp;

    if (ap->auth_mode == FL_WIFI_AUTH_OPEN || ap->auth_mode == FL_WIFI_AUTH_OWE)
        return FL_RESULT_OK;

    if (wifi_supplicant_init(&supp, ap->bssid) != 0)
        return FL_RESULT_ERR;
    if (wifi_supplicant_set_credentials(&supp, cred->ssid, cred->passphrase) != 0 ||
        wifi_supplicant_set_sta_addr(&supp, sta_mac) != 0) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }
    if (ap->auth_mode == FL_WIFI_AUTH_WPA3_SAE) {
        if (wifi_supplicant_start_sae_handshake(&supp) != 0 ||
            fl_net_wifi_sae_derive_pmk(cred->ssid, cred->passphrase, supp.keys.pmk,
                                       FL_NET_WIFI_PMK_LEN) != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
    } else if (wifi_supplicant_derive_pmk_psk(&supp, cred->ssid, cred->passphrase) != 0 ||
               wifi_supplicant_start_4way_handshake(&supp) != 0) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }
    memcpy(pmk, supp.keys.pmk, FL_NET_WIFI_PMK_LEN);
    (void)wifi_supplicant_deinit(&supp);
    return FL_RESULT_OK;
}

static fl_result_t lab_run_mgmt_assoc(const fl_net_wifi_cred_t *cred,
				      const fl_net_wifi_scan_entry_t *ap,
				      const uint8_t sta_mac[6], uint8_t pmk[FL_NET_WIFI_PMK_LEN])
{
    uint8_t probe[128];
    uint8_t auth[64];
    uint8_t assoc[200];
    size_t frame_len = 0;
    fl_result_t rc;

    if (fl_net_wifi_mgmt_build_probe_req(cred->ssid, probe, sizeof(probe), &frame_len) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_net_wifi_mgmt_build_auth_req(sta_mac, ap->bssid, auth, sizeof(auth), &frame_len) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap->bssid, sta_mac, ap->auth_mode,
                                         assoc, sizeof(assoc), &frame_len) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    if (ap->auth_mode != FL_WIFI_AUTH_OPEN) {
        rc = fl_net_wifi_wpa4_install_ptk(pmk, FL_NET_WIFI_PMK_LEN);
        if (rc != FL_RESULT_OK)
            return rc;
    }
    (void)probe;
    (void)auth;
    (void)assoc;
    return FL_RESULT_OK;
}

static void lab_fill_he(const fl_net_wifi_scan_entry_t *ap, fl_net_wifi_he_cap_t *he_out)
{
    memset(he_out, 0, sizeof(*he_out));
    if (!ap->he_supported)
        return;
    he_out->supports_ofdma = 1;
    he_out->supports_mu_mimo = 1;
    he_out->supports_twt = ap->twt_responder;
    he_out->bss_color = ap->bss_color;
    he_out->channel_width_mhz = ap->channel_width_mhz ? ap->channel_width_mhz : 20u;
    he_out->max_nss_rx = 2;
    he_out->max_nss_tx = 2;
    he_out->supports_6ghz = (ap->band == FL_WIFI_BAND_6GHZ) ? 1u : 0u;
}

void wifi_lab_reset(void)
{
    s_lab_scan_count = 0;
    memset(s_lab_scan, 0, sizeof(s_lab_scan));
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_lab_connected = 0;
    fl_net_wifi_wpa_lab_reset();
}

fl_result_t wifi_lab_scan(uint8_t band, unsigned timeout_ms)
{
    (void)timeout_ms;
    lab_seed_scan(band);
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
				 size_t *count_out)
{
    size_t i;

    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;
    *count_out = 0;
    for (i = 0; i < s_lab_scan_count && i < cap; i++) {
        entries[i] = s_lab_scan[i];
        (*count_out)++;
    }
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_connect(const fl_net_wifi_cred_t *cred,
			     fl_net_wifi_scan_entry_t *ap_out,
			     fl_net_wifi_he_cap_t *he_out)
{
    const fl_net_wifi_scan_entry_t *ap;
    static fl_net_wifi_scan_entry_t s_synth_ap;
    uint8_t pmk[FL_NET_WIFI_PMK_LEN];
    uint8_t sta_mac[6];
    fl_result_t rc;

    if (!cred || !cred->ssid[0] || !ap_out || !he_out)
        return FL_RESULT_INVAL;

    ap = lab_find_ssid(cred->ssid);
    if (!ap) {
        memset(&s_synth_ap, 0, sizeof(s_synth_ap));
        strncpy(s_synth_ap.ssid, cred->ssid, sizeof(s_synth_ap.ssid) - 1u);
        memcpy(s_synth_ap.bssid, cred->bssid, 6u);
        s_synth_ap.auth_mode = cred->auth_mode ? cred->auth_mode :
                               (cred->passphrase[0] ? FL_WIFI_AUTH_WPA2_PSK
                                                    : FL_WIFI_AUTH_OPEN);
        s_synth_ap.band = cred->band_hint ? cred->band_hint : FL_WIFI_BAND_2GHZ;
        s_synth_ap.channel = 6;
        s_synth_ap.channel_width_mhz = 20;
        s_synth_ap.rssi_dbm = -70;
        lab_apply_ax_ap(&s_synth_ap);
        ap = &s_synth_ap;
    }
    if (ap->auth_mode != FL_WIFI_AUTH_OPEN && cred->passphrase[0] == '\0')
        return FL_RESULT_INVAL;

    fl_net_loopback_mac_host(sta_mac);
    memset(pmk, 0, sizeof(pmk));
    rc = lab_supplicant_auth(cred, ap, sta_mac, pmk);
    if (rc != FL_RESULT_OK)
        rc = lab_derive_pmk(cred, pmk);
    if (rc != FL_RESULT_OK) {
        fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
        return rc;
    }

    rc = lab_run_mgmt_assoc(cred, ap, sta_mac, pmk);
    fl_net_wifi_crypto_memzero(pmk, sizeof(pmk));
    if (rc != FL_RESULT_OK)
        return rc;

    *ap_out = *ap;
    lab_fill_he(ap, he_out);
    s_negotiated_he = *he_out;
    s_lab_connected = 1;
    return FL_RESULT_OK;
}

fl_result_t wifi_lab_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (!s_lab_connected)
        return FL_RESULT_ERR;
    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}
