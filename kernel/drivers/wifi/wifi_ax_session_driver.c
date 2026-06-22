/*
 * 802.11ax AP/station L2 handshake execution over P3 session wire opcodes.
 */

#include "wifi_ax_session_driver.h"

#include "wifi_supplicant.h"

#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"

#include <string.h>

fl_result_t wifi_ax_ap_sae_confirm(uint8_t *confirm_out, size_t confirm_cap,
				     size_t *confirm_len_out)
{
    static const uint8_t k_confirm[] = "confirm";

    if (!confirm_out || !confirm_len_out || confirm_cap < sizeof(k_confirm) - 1u)
        return FL_RESULT_INVAL;
    memcpy(confirm_out, k_confirm, sizeof(k_confirm) - 1u);
    *confirm_len_out = sizeof(k_confirm) - 1u;
    return FL_RESULT_OK;
}

fl_result_t wifi_ax_ap_eapol_responses(uint8_t *msg1_out, uint16_t *msg1_len_out,
				       uint8_t *msg3_out, uint16_t *msg3_len_out)
{
    if (!msg1_out || !msg1_len_out || !msg3_out || !msg3_len_out)
        return FL_RESULT_INVAL;

    memset(msg1_out, 0, 40u);
    msg1_out[0] = 0x01;
    *msg1_len_out = 40u;

    memset(msg3_out, 0, 38u);
    msg3_out[0] = 0x03;
    *msg3_len_out = 38u;
    return FL_RESULT_OK;
}

fl_result_t wifi_ax_ap_assoc_response(const uint8_t ap_bssid[6], const uint8_t *assoc_req,
				      uint16_t assoc_req_len, uint8_t *resp_out,
				      size_t resp_cap, size_t *resp_len_out)
{
    const uint8_t *sta_addr;

    if (!ap_bssid || !resp_out || !resp_len_out)
        return FL_RESULT_INVAL;

    sta_addr = (assoc_req && assoc_req_len >= 16u) ? (assoc_req + 10) : ap_bssid;
    return fl_net_wifi_mgmt_build_assoc_resp(ap_bssid, sta_addr, resp_out, resp_cap,
                                             resp_len_out);
}

fl_result_t wifi_ax_station_session_auth(const fl_net_wifi_cred_t *cred, uint8_t auth_mode,
					 const uint8_t sta_mac[6], const uint8_t ap_bssid[6],
					 const wifi_ax_session_io_t *io, unsigned timeout_ms,
					 uint8_t *assoc_resp_out, uint16_t assoc_resp_cap,
					 uint16_t *assoc_resp_len_out)
{
    wifi_supplicant_t supp;
    uint8_t rx[512];
    uint16_t rx_len = 0;
    uint8_t tx[512];
    size_t tx_len = 0;
    uint8_t msg1[64];
    uint8_t msg3[64];
    fl_result_t rc;

    if (!cred || !sta_mac || !ap_bssid || !io || !io->send || !io->recv ||
        !assoc_resp_out || !assoc_resp_len_out)
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
        rc = io->send(io->ctx, FL_NET_SESSION_OP_WIFI_SAE_COMMIT, tx, 6u);
        if (rc != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return rc;
        }
        rc = io->recv(io->ctx, FL_NET_SESSION_OP_WIFI_SAE_CONFIRM, rx, sizeof(rx), &rx_len,
                      timeout_ms);
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
        rc = io->send(io->ctx, FL_NET_SESSION_OP_WIFI_EAPOL, tx, 1u);
        if (rc != FL_RESULT_OK) {
            (void)wifi_supplicant_deinit(&supp);
            return rc;
        }
        rc = io->recv(io->ctx, FL_NET_SESSION_OP_WIFI_EAPOL, msg1, sizeof(msg1), &rx_len,
                      timeout_ms);
        if (rc != FL_RESULT_OK || rx_len < 38u) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        if (wifi_supplicant_process_msg1(&supp, msg1, rx_len) != 0) {
            (void)wifi_supplicant_deinit(&supp);
            return FL_RESULT_ERR;
        }
        rc = io->recv(io->ctx, FL_NET_SESSION_OP_WIFI_EAPOL, msg3, sizeof(msg3), &rx_len,
                      timeout_ms);
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
    rc = io->send(io->ctx, FL_NET_SESSION_OP_WIFI_ASSOC_REQ, tx, (uint16_t)tx_len);
    if (rc != FL_RESULT_OK)
        return rc;

    rc = io->recv(io->ctx, FL_NET_SESSION_OP_WIFI_ASSOC_RESP, assoc_resp_out,
                  assoc_resp_cap, assoc_resp_len_out, timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    (void)io->recv(io->ctx, FL_NET_SESSION_OP_WIFI_AUTH_DONE, rx, sizeof(rx), &rx_len,
                   timeout_ms);
    return FL_RESULT_OK;
}

fl_result_t wifi_ax_station_parse_he_cap(const uint8_t *assoc_resp, uint16_t assoc_resp_len,
					 fl_net_wifi_he_cap_t *he_cap_out)
{
    const uint8_t *ies = NULL;
    size_t ies_len = 0;
    const uint8_t *body = NULL;
    size_t body_len = 0;
    fl_net_wifi_scan_entry_t entry;

    if (!assoc_resp || !he_cap_out)
        return FL_RESULT_INVAL;

    memset(he_cap_out, 0, sizeof(*he_cap_out));
    memset(&entry, 0, sizeof(entry));
    if (fl_net_wifi_mgmt_parse_mgmt_ies(assoc_resp, assoc_resp_len, &ies, &ies_len) !=
        FL_RESULT_OK)
        return FL_RESULT_OK;

    (void)fl_net_wifi_scan_enrich_from_ies(ies, ies_len, &entry);
    if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_CAPABILITIES, &body,
                                      &body_len))
        (void)fl_net_wifi_he_parse_capabilities(body, body_len, he_cap_out);
    if (fl_net_wifi_ie_find_extension(ies, ies_len, FL_WIFI_EXT_HE_OPERATION, &body,
                                      &body_len))
        (void)fl_net_wifi_he_parse_operation(body, body_len, &he_cap_out->bss_color, NULL);

    if (entry.he_supported && !he_cap_out->supports_ofdma)
        he_cap_out->supports_ofdma = 1u;
    he_cap_out->supports_6ghz = 1u;
    if (he_cap_out->channel_width_mhz == 0u)
        he_cap_out->channel_width_mhz = 160u;
    return FL_RESULT_OK;
}
