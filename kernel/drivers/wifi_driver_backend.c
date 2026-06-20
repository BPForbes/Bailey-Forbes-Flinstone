/*
 * WiFi Driver Backend Implementation
 * Bridges v4.3.0 drivers to contract_p3_wifi.h
 * Routes to Phase 1 (coprocessor) or Phase 4 (FullMAC) when available
 * Falls back to lab simulation when no hardware present
 */

#include "wifi_driver_backend.h"
#include "wifi_coprocessor.h"
#include "wifi_fullmac.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static wifi_backend_type_t s_backend_type = WIFI_BACKEND_NONE;
static wifi_coproc_t *s_coprocessor = NULL;
static wifi_fullmac_t *s_fullmac = NULL;

static fl_result_t wifi_int_to_result(int rc)
{
	if (rc == 0)
		return FL_RESULT_OK;
	return FL_RESULT_ERR;
}

static wifi_auth_mode_t wifi_auth_from_cred(const fl_net_wifi_cred_t *cred)
{
	uint8_t auth_mode = cred->auth_mode ? cred->auth_mode : FL_WIFI_AUTH_WPA2_PSK;

	if (auth_mode == FL_WIFI_AUTH_OPEN || cred->passphrase[0] == '\0')
		return WIFI_AUTH_OPEN;
	if (auth_mode == FL_WIFI_AUTH_WPA3_SAE)
		return WIFI_AUTH_WPA3_SAE;
	return WIFI_AUTH_WPA2_PSK;
}

/* Initialize driver backend */
fl_result_t wifi_driver_backend_init(void)
{
	/* Phase 1: Try coprocessor first (ESP32/ESP8266 over UART) */
	if (wifi_coproc_create("wlan0", &s_coprocessor) == 0) {
		/* Only enable when transport ops are registered and init succeeds. */
		if (s_coprocessor->ops && wifi_coproc_init(s_coprocessor) == 0) {
			s_backend_type = WIFI_BACKEND_COPROCESSOR;
			return FL_RESULT_OK;
		}
		wifi_coproc_destroy(s_coprocessor);
		s_coprocessor = NULL;
	}

	/* Phase 4: Try real FullMAC WiFi 6 hardware (when available) */
	/* TODO: Implement phase 4 PCIe device enumeration */

	s_backend_type = WIFI_BACKEND_NONE;
	return FL_RESULT_NOSYS;
}

wifi_backend_type_t wifi_driver_backend_active(void)
{
	return s_backend_type;
}

fl_result_t wifi_driver_scan(uint8_t band, unsigned timeout_ms)
{
	(void)band;
	(void)timeout_ms;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return wifi_int_to_result(wifi_coproc_scan(s_coprocessor));

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac)
		return wifi_int_to_result(s_fullmac->ops->start_scan(s_fullmac, NULL));

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_scan_result(fl_net_wifi_scan_entry_t *entries,
				    size_t cap, size_t *count_out)
{
	if (!entries || !count_out || cap == 0)
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		/* TODO: Parse AT+CWLAP results into fl_net_wifi_scan_entry_t. */
		(void)s_coprocessor;
		*count_out = 0;
		return FL_RESULT_NOSYS;
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		wifi_network_t networks[32];
		uint16_t count = (uint16_t)(cap < 32u ? cap : 32u);
		fl_result_t rc;
		size_t i;

		rc = wifi_int_to_result(
			s_fullmac->ops->get_scan_results(s_fullmac, networks, &count));
		if (rc != FL_RESULT_OK)
			return rc;
		*count_out = 0;
		for (i = 0; i < (size_t)count && i < cap; i++) {
			fl_net_wifi_scan_entry_t *e = &entries[i];
			memset(e, 0, sizeof(*e));
			strncpy(e->ssid, networks[i].ssid, sizeof(e->ssid) - 1u);
			memcpy(e->bssid, networks[i].bssid, sizeof(e->bssid));
			e->rssi_dbm = networks[i].rssi;
			e->channel = networks[i].channel;
			e->band = FL_WIFI_BAND_ANY;
			e->channel_width_mhz = 20;
			(*count_out)++;
		}
		return FL_RESULT_OK;
	}

	*count_out = 0;
	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_connect(const fl_net_wifi_cred_t *cred,
				unsigned timeout_ms)
{
	(void)timeout_ms;

	if (!cred || !cred->ssid[0])
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		return wifi_int_to_result(wifi_coproc_join(s_coprocessor, cred->ssid,
							   cred->passphrase,
							   wifi_auth_from_cred(cred)));
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* TODO: Coordinate with Phase 3 supplicant for 4-way/SAE */
		(void)s_fullmac;
		return FL_RESULT_NOSYS;
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_disconnect(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return wifi_int_to_result(wifi_coproc_disconnect(s_coprocessor));

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac)
		return wifi_int_to_result(s_fullmac->ops->deauthenticate(s_fullmac, 1));

	return FL_RESULT_NOSYS;
}

fl_net_wifi_state_t wifi_driver_state(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		wifi_coproc_status_t status = wifi_coproc_get_status(s_coprocessor);
		switch (status) {
		case WIFI_STATUS_DOWN:
		case WIFI_STATUS_INITIALIZING:
		case WIFI_STATUS_FIRMWARE_READY:
			return FL_WIFI_STATE_IDLE;
		case WIFI_STATUS_SCANNING:
		case WIFI_STATUS_SCAN_COMPLETE:
			return FL_WIFI_STATE_SCANNING;
		case WIFI_STATUS_AUTHENTICATING:
			return FL_WIFI_STATE_AUTHING;
		case WIFI_STATUS_ASSOCIATING:
		case WIFI_STATUS_ASSOCIATED:
			return FL_WIFI_STATE_ASSOC;
		case WIFI_STATUS_CONNECTED:
			return FL_WIFI_STATE_CONNECTED;
		case WIFI_STATUS_ERROR:
			return FL_WIFI_STATE_ERROR;
		default:
			return FL_WIFI_STATE_ERROR;
		}
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		switch (s_fullmac->state) {
		case WIFI_FULLMAC_STATE_IDLE:
			return FL_WIFI_STATE_IDLE;
		case WIFI_FULLMAC_STATE_SCANNING:
			return FL_WIFI_STATE_SCANNING;
		case WIFI_FULLMAC_STATE_CONNECTED:
			return FL_WIFI_STATE_CONNECTED;
		default:
			return FL_WIFI_STATE_ERROR;
		}
	}

	return FL_WIFI_STATE_IDLE;
}

fl_result_t wifi_driver_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
	if (!cap_out)
		return FL_RESULT_INVAL;

	memset(cap_out, 0, sizeof(*cap_out));

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		cap_out->max_nss_rx = 2;
		cap_out->max_nss_tx = 2;
		cap_out->supports_ofdma = 0;
		cap_out->supports_mu_mimo = 0;
		cap_out->supports_twt = 0;
		cap_out->supports_6ghz = 0;
		cap_out->channel_width_mhz = 20;
		return FL_RESULT_OK;
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		if (s_fullmac->ops->get_he_capabilities) {
			wifi_fullmac_he_cap_t fw_cap;
			if (s_fullmac->ops->get_he_capabilities(s_fullmac, &fw_cap) == 0) {
				cap_out->max_nss_rx = (fw_cap.mcs_nss[0] >> 4) & 0x0F;
				cap_out->max_nss_tx = (fw_cap.mcs_nss[1] >> 4) & 0x0F;
				cap_out->supports_ofdma =
					(fw_cap.ofdma_dl_supported ||
					 fw_cap.ofdma_ul_supported) ? 1 : 0;
				cap_out->supports_mu_mimo = 1;
				cap_out->supports_twt = 1;
				cap_out->supports_6ghz = 0;
				cap_out->bss_color = 0;
				cap_out->channel_width_mhz = 80;
				return FL_RESULT_OK;
			}
		}
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_twt_setup(const fl_net_wifi_twt_params_t *req,
				  fl_net_wifi_twt_params_t *agreed_out)
{
	if (!req || !agreed_out)
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		wifi_fullmac_twt_setup_t fw_req = {
			.flow_id = req->flow_id,
			.wake_interval_ms = req->wake_interval_us / 1000,
			.wake_duration_us = req->wake_duration_us,
			.requestor = 1,
			.trigger = req->trigger_enabled,
		};
		if (s_fullmac->ops->setup_twt(s_fullmac, &fw_req) == 0) {
			memcpy(agreed_out, req, sizeof(*agreed_out));
			return FL_RESULT_OK;
		}
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_twt_teardown(uint8_t flow_id)
{
	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac)
		return wifi_int_to_result(s_fullmac->ops->teardown_twt(s_fullmac, flow_id));

	return FL_RESULT_NOSYS;
}

fl_net_driver_t *wifi_driver_netdev(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return s_coprocessor->netdev;

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac)
		return s_fullmac->netdev;

	return NULL;
}
