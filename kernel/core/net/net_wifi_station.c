#include "net_wifi_station.h"

#include "net_iface.h"
#include "net_netdev.h"
#include "net_wifi_netdev.h"
#include "net_route.h"
#include "net_wifi_crypto.h"
#include "net_wifi_he.h"
#include "fl/platform.h"
#include "net_ipv4.h"
#include "net_wifi_mgmt.h"
#include "net_wifi_sae.h"
#include "net_wifi_twt.h"
#include "net_wifi_wpa.h"
#include "net_wire.h"

/* v4.3.0 First-principles WiFi driver backend (replaces external Linux dependency) */
#include "../../drivers/wifi_driver_backend.h"

#include <stdlib.h>
#include <string.h>

#define FL_NET_WIFI_HOSTED_LAB 1

static fl_net_wifi_state_t s_wifi_state = FL_WIFI_STATE_IDLE;
static fl_net_wifi_he_cap_t s_negotiated_he;
static int s_netdev_ready;
static int s_host_backend;
static int s_lab_backend;

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
    /* User-configured home network: set FL_NET_WIFI_HOME_SSID to make it visible
     * in lab scans.  FL_NET_WIFI_HOME_AUTH (open/wpa2/wpa3) and
     * FL_NET_WIFI_HOME_BAND (2/5/6) are optional; defaults are wpa2 and 2.4 GHz. */
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
                home_channel = 37; /* 6 GHz PSC channel (6135 MHz); ch 6 is 2.4 GHz */
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
            }
        }
    }
}
#endif

fl_result_t fl_net_wifi_station_init(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_wifi_state = FL_WIFI_STATE_IDLE;
    s_netdev_ready = 0;
    s_host_backend = 0;
    s_lab_backend = 0;

    /* Initialize v4.3.0 first-principles driver backend (Phase 1-4) */
    if (wifi_driver_backend_init() == FL_RESULT_OK) {
        s_host_backend = 1; /* Mark that a hardware backend is active */
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_scan_count = 0;
    s_lab_joined_ssid[0] = '\0';
    fl_net_wifi_wpa_lab_reset();
    fl_net_wifi_twt_lab_reset();
#endif
    return FL_RESULT_OK;
}

fl_net_driver_t *fl_net_wifi_station_netdev(void) {
    /* Try driver backend first (Phase 1-4 hardware) */
    if (s_host_backend) {
        fl_net_driver_t *drv = wifi_driver_netdev();
        if (drv)
            return drv;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    if (s_wifi_state == FL_WIFI_STATE_UP || s_wifi_state == FL_WIFI_STATE_DHCP ||
        s_wifi_state == FL_WIFI_STATE_CONNECTED) {
        fl_net_driver_t *wlan = fl_net_wifi_netdev_driver();
        if (wlan)
            return wlan;
    }
#endif
    return NULL;
}

int fl_net_wifi_station_host_backend(void) {
    return s_host_backend;
}

int fl_net_wifi_station_lab_backend(void) {
    return s_lab_backend;
}

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms) {
    s_lab_backend = 0;

    /* Try v4.3.0 driver backend first (Phase 1-4 hardware) */
    if (s_host_backend) {
        fl_result_t rc = wifi_driver_scan(band, timeout_ms);
        if (rc == FL_RESULT_OK) {
            s_wifi_state = FL_WIFI_STATE_SCANNING;
            return FL_RESULT_OK;
        }
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    /* Fall back to lab simulation */
    s_lab_backend = 1;
    lab_seed_scan(band);
    s_wifi_state = FL_WIFI_STATE_SCANNING;
    return FL_RESULT_OK;
#else
    (void)band;
    (void)timeout_ms;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                    size_t *count_out) {
    size_t i;

    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;

    /* Try driver backend first (Phase 1-4 hardware) */
    if (s_host_backend && !s_lab_backend) {
        fl_result_t rc = wifi_driver_scan_result(entries, cap, count_out);
        s_wifi_state = FL_WIFI_STATE_IDLE;
        return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    /* Lab simulation fallback */
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

/* Connect via v4.3.0 driver backend (Phase 1-4) */
static fl_result_t driver_backend_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    fl_result_t rc;
    fl_net_wifi_scan_entry_t ap;
    uint8_t sta_mac[6];
    size_t i;

    /* Initiate connection via hardware backend */
    rc = wifi_driver_connect(cred, timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    /* Populate AP info for netdev */
    memset(&ap, 0, sizeof(ap));
    strncpy(ap.ssid, cred->ssid, sizeof(ap.ssid) - 1u);
    if (cred->bssid[0] | cred->bssid[1] | cred->bssid[2] | cred->bssid[3] |
        cred->bssid[4] | cred->bssid[5]) {
        memcpy(ap.bssid, cred->bssid, 6);
    } else {
        /* Try to get BSSID from recent scan */
        fl_net_wifi_scan_entry_t scan[32];
        size_t n = 0;
        if (wifi_driver_scan_result(scan, 32, &n) == FL_RESULT_OK) {
            for (i = 0; i < n; i++) {
                if (!strcmp(scan[i].ssid, cred->ssid)) {
                    ap = scan[i];
                    break;
                }
            }
        }
    }

    /* Register netdev with driver backend */
    fl_net_loopback_mac_host(sta_mac);
    rc = fl_net_wifi_netdev_up(&ap, sta_mac);
    if (rc != FL_RESULT_OK) {
        wifi_driver_disconnect();
        return rc;
    }

    s_wifi_state = FL_WIFI_STATE_UP;
    strncpy(s_lab_joined_ssid, cred->ssid, sizeof(s_lab_joined_ssid) - 1u);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;

    /* Try v4.3.0 driver backend first (Phase 1-4 hardware) */
    if (s_host_backend) {
        fl_result_t rc = driver_backend_connect(cred, timeout_ms);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_NOSYS) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return rc;
        }
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    /* Fall back to lab simulation */
    (void)timeout_ms;
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
            /* Network not seen in last scan — synthesise a minimal entry so the
             * user can join by SSID + password without requiring a prior scan
             * match (handles home Wi-Fi, hidden networks, and networks visible
             * only via FL_NET_WIFI_HOME_SSID). */
            /* Single-threaded path: fl_net_wifi_connect is serialised by the
             * caller; s_synth_ap is safe without a lock. */
            static fl_net_wifi_scan_entry_t s_synth_ap;
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
            ap = &s_synth_ap;
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

    /* Disconnect via driver backend if active */
    if (s_host_backend) {
        wifi_driver_disconnect();
    }

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
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;

    /* Try driver backend first (Phase 4: real WiFi 6 hardware) */
    if (s_host_backend) {
        fl_result_t rc = wifi_driver_twt_setup(req, agreed_out);
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    /* Lab simulation fallback */
    return fl_net_wifi_twt_negotiate(req, agreed_out);
#else
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_he_cap(fl_net_wifi_he_cap_t *cap_out) {
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;

    /* Try driver backend first (Phase 4: real WiFi 6 hardware) */
    if (s_host_backend) {
        fl_result_t rc = wifi_driver_he_cap(cap_out);
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

    /* Use negotiated HE from lab or driver */
    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}
