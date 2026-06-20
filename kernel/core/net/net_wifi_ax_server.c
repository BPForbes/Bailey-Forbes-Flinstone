/*
 * 802.11ax AP/station L2 handshake relay over P3 server session wire.
 */

#include "net_wifi_ax_server.h"

#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"
#include "wifi_supplicant.h"

#include <stdio.h>
#include <string.h>

#define AX_AP_CFG_MAX 4u

typedef struct {
    fl_net_server_t *srv;
    fl_net_wifi_ax_ap_config_t cfg;
    int enabled;
} ax_ap_slot_t;

static ax_ap_slot_t s_ax_ap[AX_AP_CFG_MAX];

static ax_ap_slot_t *ax_ap_for_server(fl_net_server_t *srv)
{
    size_t i;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv)
            return &s_ax_ap[i];
    }
    return NULL;
}

fl_result_t fl_net_wifi_ax_ap_enable(fl_net_server_t *srv,
                                     const fl_net_wifi_ax_ap_config_t *cfg)
{
    size_t i;
    size_t slot = AX_AP_CFG_MAX;

    if (!srv || !cfg)
        return FL_RESULT_INVAL;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv) {
            s_ax_ap[i].cfg = *cfg;
            return FL_RESULT_OK;
        }
        if (!s_ax_ap[i].enabled && slot == AX_AP_CFG_MAX)
            slot = i;
    }
    if (slot >= AX_AP_CFG_MAX)
        return FL_RESULT_ERR;
    s_ax_ap[slot].srv = srv;
    s_ax_ap[slot].cfg = *cfg;
    s_ax_ap[slot].enabled = 1;
    return FL_RESULT_OK;
}

void fl_net_wifi_ax_ap_disable(fl_net_server_t *srv)
{
    size_t i;
    for (i = 0; i < AX_AP_CFG_MAX; i++) {
        if (s_ax_ap[i].enabled && s_ax_ap[i].srv == srv) {
            memset(&s_ax_ap[i], 0, sizeof(s_ax_ap[i]));
            return;
        }
    }
}

static fl_result_t ax_send(fl_net_sock_handle_t peer, uint8_t opcode,
                           const uint8_t *payload, uint16_t plen)
{
    if (peer == FL_NET_SOCK_INVALID)
        return FL_RESULT_ERR;
    return fl_net_session_send_frame(peer, opcode, payload, plen);
}

static fl_result_t ax_station_recv_opcode(fl_net_sock_handle_t peer, uint8_t expect,
                                          uint8_t *payload, uint16_t cap,
                                          uint16_t *plen_out, unsigned timeout_ms)
{
    uint8_t opcode = 0;
    fl_result_t rc;
    unsigned spins = timeout_ms ? timeout_ms / 10u + 1u : 500u;

    if (!payload || !plen_out)
        return FL_RESULT_INVAL;
    while (spins-- > 0u) {
        rc = fl_net_session_recv_frame(peer, &opcode, payload, cap, plen_out, 10u);
        if (rc == FL_RESULT_OK && opcode == expect)
            return FL_RESULT_OK;
        if (rc != FL_RESULT_OK && rc != FL_RESULT_TIMEDOUT)
            return rc;
    }
    return FL_RESULT_TIMEDOUT;
}

int fl_net_wifi_ax_ap_dispatch(fl_net_server_t *srv, fl_net_server_member_id_t from,
                               fl_net_sock_handle_t peer, uint8_t opcode,
                               const uint8_t *payload, uint16_t plen)
{
    ax_ap_slot_t *ap = ax_ap_for_server(srv);
    uint8_t reply[512];
    size_t reply_len = 0;
    uint8_t msg1[64];
    uint8_t msg3[64];

    (void)from;
    if (!ap || !fl_net_session_is_wifi_opcode(opcode))
        return 0;

    switch (opcode) {
    case FL_NET_SESSION_OP_WIFI_SAE_COMMIT:
        if (ap->cfg.auth_mode != FL_WIFI_AUTH_WPA3_SAE || plen == 0u)
            return 1;
        (void)memcpy(reply, (const uint8_t *)"confirm", 7u);
        reply_len = 7u;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_SAE_CONFIRM, reply,
                      (uint16_t)reply_len);
        return 1;

    case FL_NET_SESSION_OP_WIFI_EAPOL:
        if (ap->cfg.auth_mode != FL_WIFI_AUTH_WPA2_PSK)
            return 1;
        memset(msg1, 0, sizeof(msg1));
        msg1[0] = 0x01;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_EAPOL, msg1, 40);
        memset(msg3, 0, sizeof(msg3));
        msg3[0] = 0x03;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_EAPOL, msg3, 38);
        return 1;

    case FL_NET_SESSION_OP_WIFI_ASSOC_REQ:
        if (fl_net_wifi_mgmt_build_assoc_resp(
                ap->cfg.bssid,
                (plen >= 16u) ? (payload + 10) : ap->cfg.bssid, reply, sizeof(reply),
                &reply_len) != FL_RESULT_OK)
            return 1;
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_ASSOC_RESP, reply,
                      (uint16_t)reply_len);
        {
            const uint8_t *ies = NULL;
            size_t ies_len = 0;
            if (fl_net_wifi_mgmt_parse_mgmt_ies(reply, reply_len, &ies, &ies_len) ==
                FL_RESULT_OK) {
                (void)ies;
                (void)ies_len;
            }
        }
        (void)ax_send(peer, FL_NET_SESSION_OP_WIFI_AUTH_DONE, (const uint8_t *)"ok", 2u);
        return 1;

    default:
        return 1;
    }
}

fl_result_t fl_net_wifi_ax_station_ota(fl_net_client_t *client,
                                       const fl_net_wifi_cred_t *cred,
                                       uint8_t auth_mode, const uint8_t sta_mac[6],
                                       const uint8_t ap_bssid[6],
                                       fl_net_wifi_he_cap_t *he_cap_out,
                                       unsigned timeout_ms)
{
    wifi_supplicant_t supp;
    uint8_t rx[512];
    uint16_t rx_len = 0;
    uint8_t tx[512];
    size_t tx_len = 0;
    uint8_t msg1[64];
    uint8_t msg3[64];
    fl_result_t rc;

    if (!client || client->peer_handle == FL_NET_SOCK_INVALID || !cred || !sta_mac ||
        !ap_bssid)
        return FL_RESULT_INVAL;

    if (wifi_supplicant_init(&supp, ap_bssid) != 0)
        return FL_RESULT_ERR;
    if (wifi_supplicant_set_credentials(&supp, cred->ssid, cred->passphrase) != 0 ||
        wifi_supplicant_set_sta_addr(&supp, sta_mac) != 0) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }

    if (auth_mode == FL_WIFI_AUTH_WPA3_SAE) {
        if (wifi_supplicant_start_sae_handshake(&supp) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        memcpy(tx, (const uint8_t *)"commit", 6u);
        rc = ax_send(client->peer_handle, FL_NET_SESSION_OP_WIFI_SAE_COMMIT, tx, 6u);
        if (rc != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return rc;
        }
        rc = ax_station_recv_opcode(client->peer_handle,
                                    FL_NET_SESSION_OP_WIFI_SAE_CONFIRM, rx,
                                    sizeof(rx), &rx_len, timeout_ms);
        if (rc != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return rc;
        }
        if (wifi_supplicant_process_sae_confirm(&supp, rx, rx_len) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
    } else if (auth_mode == FL_WIFI_AUTH_WPA2_PSK) {
        if (wifi_supplicant_derive_pmk_psk(&supp, cred->ssid, cred->passphrase) != 0 ||
            wifi_supplicant_start_4way_handshake(&supp) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        tx[0] = 0x02;
        rc = ax_send(client->peer_handle, FL_NET_SESSION_OP_WIFI_EAPOL, tx, 1u);
        if (rc != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return rc;
        }
        rc = ax_station_recv_opcode(client->peer_handle, FL_NET_SESSION_OP_WIFI_EAPOL,
                                    msg1, sizeof(msg1), &rx_len, timeout_ms);
        if (rc != FL_RESULT_OK || rx_len < 38u) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        if (wifi_supplicant_process_msg1(&supp, msg1, rx_len) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        rc = ax_station_recv_opcode(client->peer_handle, FL_NET_SESSION_OP_WIFI_EAPOL,
                                    msg3, sizeof(msg3), &rx_len, timeout_ms);
        if (rc != FL_RESULT_OK || rx_len < 38u) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        if (wifi_supplicant_process_msg3(&supp, msg3, rx_len) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
    }

    if (auth_mode != FL_WIFI_AUTH_OPEN &&
        wifi_supplicant_get_state(&supp) != WIFI_SUPP_STATE_AUTHENTICATED) {
        (void)wifi_supplicant_deinit(&supp);
        return FL_RESULT_ERR;
    }
    (void)wifi_supplicant_deinit(&supp);

    if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap_bssid, sta_mac, auth_mode, tx,
                                         sizeof(tx), &tx_len) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    rc = ax_send(client->peer_handle, FL_NET_SESSION_OP_WIFI_ASSOC_REQ, tx,
                 (uint16_t)tx_len);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = ax_station_recv_opcode(client->peer_handle, FL_NET_SESSION_OP_WIFI_ASSOC_RESP,
                                rx, sizeof(rx), &rx_len, timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    if (he_cap_out) {
        const uint8_t *ies = NULL;
        size_t ies_len = 0;
        const uint8_t *body = NULL;
        size_t body_len = 0;
        fl_net_wifi_scan_entry_t entry;

        memset(he_cap_out, 0, sizeof(*he_cap_out));
        memset(&entry, 0, sizeof(entry));
        if (fl_net_wifi_mgmt_parse_mgmt_ies(rx, rx_len, &ies, &ies_len) == FL_RESULT_OK) {
            (void)fl_net_wifi_scan_enrich_from_ies(ies, ies_len, &entry);
            if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_CAPABILITIES,
                                              &body, &body_len))
                (void)fl_net_wifi_he_parse_capabilities(body, body_len, he_cap_out);
            if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_OPERATION,
                                              &body, &body_len))
                (void)fl_net_wifi_he_parse_operation(body, body_len, &he_cap_out->bss_color,
                                                     NULL);
        }
        if (entry.he_supported && !he_cap_out->supports_ofdma)
            he_cap_out->supports_ofdma = 1u;
        he_cap_out->supports_6ghz = 1u;
        if (he_cap_out->channel_width_mhz == 0u)
            he_cap_out->channel_width_mhz = 160u;
    }

    (void)ax_station_recv_opcode(client->peer_handle, FL_NET_SESSION_OP_WIFI_AUTH_DONE,
                                 rx, sizeof(rx), &rx_len, timeout_ms);
    return FL_RESULT_OK;
}
