/**
 * #279 L2 OTA via P3 server host/join — SAE, EAPOL 4-way, HE Assoc Req/Resp.
 * Emulates 802.11ax AP/station exchange on the session wire (no RF).
 */
#include "contract_p3_session_wire.h"
#include "net_client.h"
#include "net_endian.h"
#include "net_server.h"
#include "net_wifi_ax_server.h"
#include "server_bg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c)                                                              \
    do {                                                                       \
        if (!(c)) {                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int pick_port(void)
{
    return 48000 + (getpid() % 1000);
}

static int test_wpa3_sae_server_ota(void)
{
    fl_net_server_t srv;
    fl_net_client_t cli;
    fl_server_bg_t *bg = NULL;
    fl_net_wifi_ax_ap_config_t ap_cfg;
    fl_net_wifi_cred_t cred;
    fl_net_wifi_he_cap_t he = {0};
    uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t ap_bssid[6] = {0x02, 0xaa, 0, 0, 0, 0x01};
    uint16_t port = (uint16_t)pick_port();
    uint32_t loop_be = fl_net_htonl(0x7F000001u); /* 127.0.0.1 */
    fl_result_t rc;

    ASSERT(fl_net_server_init(&srv) == FL_RESULT_OK);
    rc = fl_net_server_host_start(&srv, loop_be, port, "MockAxAP");
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    memset(&ap_cfg, 0, sizeof(ap_cfg));
    memcpy(ap_cfg.bssid, ap_bssid, 6);
    ap_cfg.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    strncpy(ap_cfg.ssid, "MockAx6", sizeof(ap_cfg.ssid) - 1u);
    ASSERT(fl_net_wifi_ax_ap_enable(&srv, &ap_cfg) == FL_RESULT_OK);
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    ASSERT(fl_net_client_init(&cli) == FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cli, loop_be, port, "MockAxSTA", 5000u) == FL_RESULT_OK);

    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "MockAx6", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "mock-secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;

    ASSERT(fl_net_wifi_ax_station_ota(&cli, &cred, FL_WIFI_AUTH_WPA3_SAE, sta_mac, ap_bssid,
                                    &he, 5000u) == FL_RESULT_OK);
    ASSERT(he.supports_ofdma == 1u);
    ASSERT(he.supports_6ghz == 1u);
    ASSERT(he.bss_color == 5u);
    ASSERT(he.channel_width_mhz == 160u);

    ASSERT(fl_net_client_send_msg(&cli, "server-over-wifi-ax-mock") == FL_RESULT_OK);

    fl_net_client_disconnect(&cli);
    fl_server_bg_stop_server(bg);
    fl_net_wifi_ax_ap_disable(&srv);
    fl_net_server_host_stop(&srv);
    return 0;
}

static int test_wpa2_eapol_server_ota(void)
{
    fl_net_server_t srv;
    fl_net_client_t cli;
    fl_server_bg_t *bg = NULL;
    fl_net_wifi_ax_ap_config_t ap_cfg;
    fl_net_wifi_cred_t cred;
    uint8_t sta_mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x66};
    uint8_t ap_bssid[6] = {0x02, 0xac, 0, 0, 0, 0x02};
    uint16_t port = (uint16_t)(pick_port() + 1);
    uint32_t loop_be = fl_net_htonl(0x7F000001u);
    fl_result_t rc;

    ASSERT(fl_net_server_init(&srv) == FL_RESULT_OK);
    rc = fl_net_server_host_start(&srv, loop_be, port, "LegacyACAP");
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    memset(&ap_cfg, 0, sizeof(ap_cfg));
    memcpy(ap_cfg.bssid, ap_bssid, 6);
    ap_cfg.auth_mode = FL_WIFI_AUTH_WPA2_PSK;
    strncpy(ap_cfg.ssid, "LegacyAC", sizeof(ap_cfg.ssid) - 1u);
    ASSERT(fl_net_wifi_ax_ap_enable(&srv, &ap_cfg) == FL_RESULT_OK);
    ASSERT(fl_server_bg_start_server(&srv, &bg) == FL_RESULT_OK);

    ASSERT(fl_net_client_init(&cli) == FL_RESULT_OK);
    ASSERT(fl_net_client_connect(&cli, loop_be, port, "LegacyACSTA", 5000u) == FL_RESULT_OK);

    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LegacyAC", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "mock-secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA2_PSK;

    ASSERT(fl_net_wifi_ax_station_ota(&cli, &cred, FL_WIFI_AUTH_WPA2_PSK, sta_mac, ap_bssid,
                                    NULL, 5000u) == FL_RESULT_OK);

    fl_net_client_disconnect(&cli);
    fl_server_bg_stop_server(bg);
    fl_net_wifi_ax_ap_disable(&srv);
    fl_net_server_host_stop(&srv);
    return 0;
}

int main(void)
{
    if (test_wpa3_sae_server_ota() != 0)
        return 1;
    if (test_wpa2_eapol_server_ota() != 0)
        return 1;
    puts("test_wifi_ax_server_ota: SAE + EAPOL + HE assoc via server host/join passed");
    return 0;
}
