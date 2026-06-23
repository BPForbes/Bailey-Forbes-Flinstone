/*
 * 802.11ax L2 OTA emulation over P3 server host/join (#279, Wi-Fi 5-only hosts).
 * Carries SAE commit/confirm, EAPOL 4-way, and Assoc Req/Resp with HE IEs on
 * the session wire when RF / real ax NIC is unavailable.
 * L2 execution and session-wire orchestration live in net_wifi_ax_server.c.
 */

#ifndef NET_WIFI_AX_SERVER_H
#define NET_WIFI_AX_SERVER_H

#include "contract_p3_wifi.h"
#include "contract_p3_session_wire.h"
#include "contract_result.h"
#include "net_client.h"
#include "net_server.h"

#include <stdint.h>

typedef struct {
    uint8_t bssid[6];
    uint8_t auth_mode; /* FL_WIFI_AUTH_* */
    char ssid[FL_WIFI_SSID_MAX + 1u];
} fl_net_wifi_ax_ap_config_t;

/** Enable mock 802.11ax AP handling on an active server host. */
fl_result_t fl_net_wifi_ax_ap_enable(fl_net_server_t *srv,
                                     const fl_net_wifi_ax_ap_config_t *cfg);

void fl_net_wifi_ax_ap_disable(fl_net_server_t *srv);

/**
 * Host-side dispatch for WiFi session opcodes (called from net_server poll).
 * Returns 1 when handled, 0 otherwise.
 */
int fl_net_wifi_ax_ap_dispatch(fl_net_server_t *srv, fl_net_server_member_id_t from,
                               fl_net_sock_handle_t peer, uint8_t opcode,
                               const uint8_t *payload, uint16_t plen);

/**
 * Station-side OTA auth over an established server join (client connected).
 * Runs supplicant against AP responses received on the session wire.
 */
fl_result_t fl_net_wifi_ax_station_ota(fl_net_client_t *client,
                                       const fl_net_wifi_cred_t *cred,
                                       uint8_t auth_mode, const uint8_t sta_mac[6],
                                       const uint8_t ap_bssid[6],
                                       fl_net_wifi_he_cap_t *he_cap_out,
                                       unsigned timeout_ms);

#endif /* NET_WIFI_AX_SERVER_H */
