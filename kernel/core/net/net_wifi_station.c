#include "net_wifi_station.h"

#include "net_iface.h"
#include "net_netdev.h"
#include "net_wifi_netdev.h"
#include "net_route.h"
#include "net_wifi_crypto.h"
#include "net_wifi_he.h"
#include "fl/platform.h"
#include "net_ipv4.h"
#include "net_wifi_twt.h"
#include "net_wifi_wpa.h"
#include "net_wire.h"

/* v4.3.0 WiFi driver backend — kernel orchestrates, drivers execute */
#include "wifi_driver_backend.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define FL_NET_WIFI_HOSTED_LAB 1

static fl_net_wifi_state_t s_wifi_state = FL_WIFI_STATE_IDLE;
static fl_net_wifi_he_cap_t s_negotiated_he;
static int s_netdev_ready;
static int s_driver_backend;
static int s_lab_backend;

#if defined(FL_NET_WIFI_HOSTED_LAB)
static char s_lab_joined_ssid[FL_WIFI_SSID_MAX];
#endif

void fl_net_wifi_cred_scrub_passphrase(fl_net_wifi_cred_t *cred) {
    if (cred)
        fl_net_wifi_crypto_memzero(cred->passphrase, sizeof(cred->passphrase));
}

fl_result_t fl_net_wifi_station_init(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));
    s_wifi_state = FL_WIFI_STATE_IDLE;
    s_netdev_ready = 0;
    s_driver_backend = 0;
    s_lab_backend = 0;

    if (wifi_driver_backend_init() == FL_RESULT_OK &&
        wifi_driver_backend_active() != WIFI_BACKEND_NONE)
        s_driver_backend = 1;

#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_joined_ssid[0] = '\0';
    wifi_driver_lab_reset();
    fl_net_wifi_twt_lab_reset();
#endif
    return FL_RESULT_OK;
}

fl_net_driver_t *fl_net_wifi_station_netdev(void) {
    if (s_driver_backend) {
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
    return 0;
}

int fl_net_wifi_station_lab_backend(void) {
    return s_lab_backend;
}

fl_result_t fl_net_wifi_scan(uint8_t band, unsigned timeout_ms) {
    s_lab_backend = 0;

    if (s_driver_backend) {
        fl_result_t rc = wifi_driver_scan(band, timeout_ms);
        if (rc == FL_RESULT_OK) {
            s_wifi_state = FL_WIFI_STATE_SCANNING;
            return FL_RESULT_OK;
        }
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_backend = 1;
    if (wifi_driver_lab_scan(band, timeout_ms) == FL_RESULT_OK) {
        s_wifi_state = FL_WIFI_STATE_SCANNING;
        return FL_RESULT_OK;
    }
#else
    (void)band;
    (void)timeout_ms;
#endif
    return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
                                    size_t *count_out) {
    if (!entries || !count_out || cap == 0u)
        return FL_RESULT_INVAL;

    if (s_lab_backend) {
        fl_result_t rc = wifi_driver_lab_scan_result(entries, cap, count_out);
        s_wifi_state = FL_WIFI_STATE_IDLE;
        return rc;
    }

    if (s_driver_backend) {
        fl_result_t rc = wifi_driver_scan_result(entries, cap, count_out);
        s_wifi_state = FL_WIFI_STATE_IDLE;
        return rc;
    }

    *count_out = 0u;
    return FL_RESULT_NOSYS;
}

#if defined(FL_NET_WIFI_HOSTED_LAB)
static fl_result_t lab_backend_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    fl_net_wifi_scan_entry_t ap;
    uint8_t sta_mac[6];
    fl_result_t rc;

    (void)timeout_ms;
    s_wifi_state = FL_WIFI_STATE_AUTHING;
    rc = wifi_driver_lab_connect(cred, &ap, &s_negotiated_he);
    if (rc != FL_RESULT_OK) {
        s_wifi_state = FL_WIFI_STATE_ERROR;
        return rc;
    }

    strncpy(s_lab_joined_ssid, cred->ssid, sizeof(s_lab_joined_ssid) - 1u);
    s_wifi_state = FL_WIFI_STATE_CONNECTED;

    fl_net_loopback_mac_host(sta_mac);
    rc = fl_net_wifi_netdev_up(&ap, sta_mac);
    if (rc != FL_RESULT_OK) {
        s_wifi_state = FL_WIFI_STATE_ERROR;
        return rc;
    }

    s_wifi_state = FL_WIFI_STATE_UP;
    fl_net_iface_refresh();
    return FL_RESULT_OK;
}
#endif

/* Connect via v4.3.0 driver backend (Phase 1-4 hardware/mock) */
static fl_result_t driver_backend_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    fl_result_t rc;
    fl_net_wifi_scan_entry_t ap;
    uint8_t sta_mac[6];
    size_t i;

    (void)timeout_ms;
    rc = wifi_driver_connect(cred, timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    memset(&ap, 0, sizeof(ap));
    strncpy(ap.ssid, cred->ssid, sizeof(ap.ssid) - 1u);
    if (cred->bssid[0] | cred->bssid[1] | cred->bssid[2] | cred->bssid[3] |
        cred->bssid[4] | cred->bssid[5]) {
        memcpy(ap.bssid, cred->bssid, 6);
    } else {
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

    fl_net_loopback_mac_host(sta_mac);
    rc = fl_net_wifi_netdev_up(&ap, sta_mac);
    if (rc != FL_RESULT_OK) {
        wifi_driver_disconnect();
        return rc;
    }

    s_wifi_state = FL_WIFI_STATE_UP;
    fl_net_iface_refresh();
    strncpy(s_lab_joined_ssid, cred->ssid, sizeof(s_lab_joined_ssid) - 1u);
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_connect(const fl_net_wifi_cred_t *cred, unsigned timeout_ms) {
    if (!cred || !cred->ssid[0])
        return FL_RESULT_INVAL;

    if (s_driver_backend) {
        fl_result_t rc = driver_backend_connect(cred, timeout_ms);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_NOSYS) {
            s_wifi_state = FL_WIFI_STATE_ERROR;
            return rc;
        }
        s_lab_backend = 1;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    s_lab_backend = 1;
    return lab_backend_connect(cred, timeout_ms);
#else
    (void)timeout_ms;
    s_wifi_state = FL_WIFI_STATE_ERROR;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_disconnect(void) {
    memset(&s_negotiated_he, 0, sizeof(s_negotiated_he));

    if (s_driver_backend)
        wifi_driver_disconnect();

#if defined(FL_NET_WIFI_HOSTED_LAB)
    if (s_lab_backend)
        wifi_driver_lab_reset();
    s_lab_joined_ssid[0] = '\0';
    fl_net_wifi_twt_lab_reset();
#endif
    fl_net_wifi_netdev_down();
    fl_net_iface_refresh();

    s_wifi_state = FL_WIFI_STATE_IDLE;
    s_lab_backend = 0;
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

    if (s_driver_backend) {
        fl_result_t rc = wifi_driver_twt_setup(req, agreed_out);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    return fl_net_wifi_twt_negotiate(req, agreed_out);
#else
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_twt_teardown(uint8_t flow_id) {
    if (s_driver_backend) {
        fl_result_t rc = wifi_driver_twt_teardown(flow_id);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    return fl_net_wifi_twt_lab_teardown(flow_id);
#else
    (void)flow_id;
    return FL_RESULT_NOSYS;
#endif
}

fl_result_t fl_net_wifi_he_cap(fl_net_wifi_he_cap_t *cap_out) {
    if (!cap_out)
        return FL_RESULT_INVAL;
    if (s_wifi_state != FL_WIFI_STATE_CONNECTED && s_wifi_state != FL_WIFI_STATE_UP &&
        s_wifi_state != FL_WIFI_STATE_DHCP)
        return FL_RESULT_ERR;

    if (s_driver_backend) {
        fl_result_t rc = wifi_driver_he_cap(cap_out);
        if (rc != FL_RESULT_NOSYS)
            return rc;
    }

#if defined(FL_NET_WIFI_HOSTED_LAB)
    if (s_lab_backend) {
        fl_result_t rc = wifi_driver_lab_he_cap(cap_out);
        if (rc == FL_RESULT_OK)
            return FL_RESULT_OK;
    }
#endif

    *cap_out = s_negotiated_he;
    return FL_RESULT_OK;
}
