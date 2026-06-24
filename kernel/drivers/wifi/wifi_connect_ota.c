/*
 * Shared production-tail connect: auth/assoc mgmt OTA, Dragonfly SAE, EAPOL 4-way (#328).
 */

#include "wifi_connect_ota.h"

#include "wifi_supplicant.h"

#include "kernel/core/net/net_wifi_mgmt.h"
#include "kernel/core/net/net_wifi_mgmt_ota.h"
#include "kernel/core/net/net_wifi_fullmac.h"
#include "kernel/core/net/net_wifi_wpa.h"
#include "kernel/core/net/net_wire.h"

#include <string.h>

#define WIFI_OTA_ETH_P_EAPOL 0x888eu
#define WIFI_OTA_TIMEOUT_MS  5000u

#define WIFI_OTA_FRAME_MAX 512u

static uint8_t wifi_auth_mode_from_ap(const wifi_network_t *ap)
{
	if (!ap)
		return FL_WIFI_AUTH_OPEN;
	switch (ap->auth_mode) {
	case WIFI_AUTH_WPA3_SAE:
		return FL_WIFI_AUTH_WPA3_SAE;
	case WIFI_AUTH_WPA2_PSK:
		return FL_WIFI_AUTH_WPA2_PSK;
	default:
		return FL_WIFI_AUTH_OPEN;
	}
}

static int wifi_ota_tx_mgmt(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	if (!tr || !tr->tx_mgmt)
		return -1;
	return tr->tx_mgmt(tr, frame, len);
}

static int wifi_ota_rx_mgmt(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out)
{
	if (!tr || !tr->rx_mgmt)
		return -1;
	return tr->rx_mgmt(tr, frame, cap, len_out, WIFI_OTA_TIMEOUT_MS);
}

static int wifi_ota_build_eapol_eth(const uint8_t sta_mac[6], const uint8_t bssid[6],
				    const uint8_t *eapol, size_t eapol_len, uint8_t *out,
				    size_t out_cap, size_t *out_len)
{
	size_t need;

	if (!sta_mac || !bssid || !eapol || !out || !out_len)
		return -1;
	need = FL_NET_ETH_FRAME_HDR_LEN + eapol_len;
	if (out_cap < need)
		return -1;
	memcpy(out, bssid, 6u);
	memcpy(out + 6, sta_mac, 6u);
	out[12] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL >> 8);
	out[13] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL & 0xffu);
	memcpy(out + FL_NET_ETH_FRAME_HDR_LEN, eapol, eapol_len);
	*out_len = need;
	return 0;
}

static int wifi_ota_eapol_payload(const uint8_t *eth, size_t eth_len, const uint8_t **eapol_out,
				  size_t *eapol_len_out)
{
	uint16_t ethertype;

	if (!eth || eth_len < FL_NET_ETH_FRAME_HDR_LEN + 1u || !eapol_out || !eapol_len_out)
		return -1;
	ethertype = (uint16_t)(((uint16_t)eth[12] << 8) | (uint16_t)eth[13]);
	if (ethertype != WIFI_OTA_ETH_P_EAPOL)
		return -1;
	*eapol_out = eth + FL_NET_ETH_FRAME_HDR_LEN;
	*eapol_len_out = eth_len - FL_NET_ETH_FRAME_HDR_LEN;
	return 0;
}

static int wifi_ota_run_sae(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			    const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			    wifi_supplicant_t *supp)
{
	static const uint8_t k_commit[] = "commit";
	uint8_t tx[WIFI_OTA_FRAME_MAX];
	uint8_t rx[WIFI_OTA_FRAME_MAX];
	size_t tx_len = 0;
	size_t rx_len = 0;

	(void)cred;
	if (wifi_supplicant_start_sae_handshake(supp) != 0)
		return -1;
	if (fl_net_wifi_mgmt_build_auth_sae_commit(sta_mac, ap->bssid, k_commit,
						    sizeof(k_commit) - 1u, tx, sizeof(tx),
						    &tx_len) != FL_RESULT_OK)
		return -1;
	if (wifi_ota_tx_mgmt(tr, tx, tx_len) != 0)
		return -1;
	if (wifi_ota_rx_mgmt(tr, rx, sizeof(rx), &rx_len) != 0)
		return -1;
	{
		uint16_t auth_alg = 0;
		uint16_t auth_seq = 0;
		uint16_t status = 0xffffu;
		const uint8_t *body = NULL;
		size_t body_len = 0;

		if (fl_net_wifi_mgmt_parse_auth_resp(rx, rx_len, &auth_alg, &auth_seq, &status, &body,
						   &body_len) != FL_RESULT_OK ||
		    auth_alg != FL_WIFI_AUTH_ALG_SAE || auth_seq != 2u || status != 0u)
			return -1;
		if (wifi_supplicant_process_sae_confirm(supp, body, body_len) != 0)
			return -1;
	}
	return 0;
}

static int wifi_ota_run_eapol(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			      const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			      wifi_supplicant_t *supp)
{
	uint8_t eapol_tx[64];
	uint8_t eth_tx[WIFI_OTA_FRAME_MAX];
	uint8_t eth_rx[WIFI_OTA_FRAME_MAX];
	size_t eth_tx_len = 0;
	size_t eth_rx_len = 0;
	const uint8_t *eapol;
	size_t eapol_len = 0;

	(void)ap;
	if (wifi_supplicant_derive_pmk_psk(supp, cred->ssid, cred->passphrase) != 0 ||
	    wifi_supplicant_start_4way_handshake(supp) != 0)
		return -1;

	memset(eapol_tx, 0, sizeof(eapol_tx));
	eapol_tx[0] = 0x02;
	if (wifi_ota_build_eapol_eth(sta_mac, supp->keys.bssid, eapol_tx, 1u, eth_tx, sizeof(eth_tx),
				     &eth_tx_len) != 0)
		return -1;
	if (!tr->tx_data || tr->tx_data(tr, eth_tx, eth_tx_len) != 0)
		return -1;
	if (!tr->rx_data || tr->rx_data(tr, eth_rx, sizeof(eth_rx), &eth_rx_len,
					WIFI_OTA_TIMEOUT_MS) != 0)
		return -1;
	if (wifi_ota_eapol_payload(eth_rx, eth_rx_len, &eapol, &eapol_len) != 0 ||
	    eapol_len < 38u)
		return -1;
	if (wifi_supplicant_process_msg1(supp, eapol, eapol_len) != 0)
		return -1;
	if (!tr->rx_data || tr->rx_data(tr, eth_rx, sizeof(eth_rx), &eth_rx_len,
					WIFI_OTA_TIMEOUT_MS) != 0)
		return -1;
	if (wifi_ota_eapol_payload(eth_rx, eth_rx_len, &eapol, &eapol_len) != 0 ||
	    eapol_len < 38u)
		return -1;
	if (wifi_supplicant_process_msg3(supp, eapol, eapol_len) != 0)
		return -1;
	return 0;
}

static int wifi_ota_run_open_auth(const uint8_t sta_mac[6], const uint8_t bssid[6],
				  wifi_mgmt_transport_t *tr)
{
	uint8_t tx[WIFI_OTA_FRAME_MAX];
	uint8_t rx[WIFI_OTA_FRAME_MAX];
	size_t tx_len = 0;
	size_t rx_len = 0;

	if (fl_net_wifi_mgmt_build_auth_req(sta_mac, bssid, tx, sizeof(tx), &tx_len) !=
	    FL_RESULT_OK)
		return -1;
	if (wifi_ota_tx_mgmt(tr, tx, tx_len) != 0)
		return -1;
	if (wifi_ota_rx_mgmt(tr, rx, sizeof(rx), &rx_len) != 0)
		return -1;
	{
		uint16_t auth_alg = 0;
		uint16_t auth_seq = 0;
		uint16_t status = 0xffffu;

		if (fl_net_wifi_mgmt_parse_auth_resp(rx, rx_len, &auth_alg, &auth_seq, &status, NULL,
						   NULL) != FL_RESULT_OK ||
		    auth_alg != FL_WIFI_AUTH_ALG_OPEN || auth_seq != 2u || status != 0u)
			return -1;
	}
	return 0;
}

static int wifi_ota_run_assoc(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			      const uint8_t sta_mac[6], uint8_t auth_mode,
			      wifi_mgmt_transport_t *tr)
{
	uint8_t tx[WIFI_OTA_FRAME_MAX];
	uint8_t rx[WIFI_OTA_FRAME_MAX];
	size_t tx_len = 0;
	size_t rx_len = 0;

	if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap->bssid, sta_mac, auth_mode, NULL, tx,
					     sizeof(tx), &tx_len) != FL_RESULT_OK)
		return -1;
	if (wifi_ota_tx_mgmt(tr, tx, tx_len) != 0)
		return -1;
	if (wifi_ota_rx_mgmt(tr, rx, sizeof(rx), &rx_len) != 0)
		return -1;
	{
		fl_net_wifi_mgmt_ota_t *ota = fl_net_wifi_fullmac_mgmt_ota();

		if (ota)
			(void)fl_net_wifi_mgmt_ota_store_assoc_resp(ota, rx, rx_len);
		else {
			uint16_t status = 0xffffu;
			fl_net_wifi_he_cap_t he;

			if (fl_net_wifi_mgmt_parse_assoc_resp(rx, rx_len, &status, NULL, &he) !=
				FL_RESULT_OK ||
			    status != 0u)
				return -1;
			fl_net_wifi_fullmac_set_negotiated_he(&he);
		}
	}
	return 0;
}

static int wifi_ota_install_keys(const wifi_connect_ota_hooks_t *hooks, wifi_supplicant_t *supp,
				 uint8_t auth_mode)
{
	if (auth_mode == FL_WIFI_AUTH_OPEN)
		return 0;
	if (wifi_supplicant_get_state(supp) != WIFI_SUPP_STATE_AUTHENTICATED)
		return -1;
	if (hooks && hooks->set_key)
		(void)hooks->set_key(hooks->ctx, supp->keys.ptk, 16u);
	(void)fl_net_wifi_wpa4_install_ptk(supp->keys.pmk, FL_NET_WIFI_PMK_LEN);
	return 0;
}

int wifi_connect_ota_run_phase(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			       const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			       const wifi_connect_ota_hooks_t *hooks,
			       wifi_connect_ota_phase_t phase)
{
	wifi_supplicant_t supp;
	uint8_t auth_mode;

	if (!cred || !ap || !sta_mac || !tr)
		return -1;

	auth_mode = wifi_auth_mode_from_ap(ap);
	if (wifi_supplicant_init(&supp, ap->bssid) != 0)
		return -1;
	if (wifi_supplicant_set_credentials(&supp, cred->ssid, cred->passphrase) != 0 ||
	    wifi_supplicant_set_sta_addr(&supp, sta_mac) != 0) {
		(void)wifi_supplicant_deinit(&supp);
		return -1;
	}

	if (phase != WIFI_CONNECT_OTA_ASSOC_ONLY) {
		if (auth_mode == FL_WIFI_AUTH_WPA3_SAE) {
			if (wifi_ota_run_sae(cred, ap, sta_mac, tr, &supp) != 0) {
				(void)wifi_supplicant_deinit(&supp);
				return -1;
			}
		} else if (auth_mode == FL_WIFI_AUTH_WPA2_PSK) {
			if (wifi_ota_run_eapol(cred, ap, sta_mac, tr, &supp) != 0) {
				(void)wifi_supplicant_deinit(&supp);
				return -1;
			}
		} else if (wifi_ota_run_open_auth(sta_mac, ap->bssid, tr) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
	}

	if (phase != WIFI_CONNECT_OTA_AUTH_ONLY) {
		if (wifi_ota_run_assoc(cred, ap, sta_mac, auth_mode, tr) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
		if (wifi_ota_install_keys(hooks, &supp, auth_mode) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
	}

	(void)wifi_supplicant_deinit(&supp);
	return 0;
}

int wifi_connect_ota_run(const fl_net_wifi_cred_t *cred, const wifi_network_t *ap,
			 const uint8_t sta_mac[6], wifi_mgmt_transport_t *tr,
			 const wifi_connect_ota_hooks_t *hooks)
{
	return wifi_connect_ota_run_phase(cred, ap, sta_mac, tr, hooks, WIFI_CONNECT_OTA_ALL);
}
