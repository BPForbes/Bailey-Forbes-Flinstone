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

/* Initialize driver backend */
fl_result_t wifi_driver_backend_init(void)
{
	/* Phase 1: Try coprocessor first (ESP32/ESP8266 over UART) */
	if (wifi_coproc_create("wlan0", &s_coprocessor) == 0) {
		s_backend_type = WIFI_BACKEND_COPROCESSOR;
		return FL_RESULT_OK;
	}

	/* Phase 4: Try real FullMAC WiFi 6 hardware (when available) */
	/* TODO: Implement phase 4 PCIe device enumeration */
	/* if (wifi_fullmac_pcie_create(vendor_id, device_id, &s_fullmac) == 0) {
	 *     s_backend_type = WIFI_BACKEND_FULLMAC;
	 *     return FL_RESULT_OK;
	 * }
	 */

	/* No hardware backend available */
	s_backend_type = WIFI_BACKEND_NONE;
	return FL_RESULT_NOSYS;
}

wifi_backend_type_t wifi_driver_backend_active(void)
{
	return s_backend_type;
}

fl_result_t wifi_driver_scan(uint8_t band, unsigned timeout_ms)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		/* Phase 1: ESP32 coprocessor scan via AT commands */
		/* AT+CWLAP returns list of available networks */
		return wifi_coproc_scan(s_coprocessor);
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* Phase 4: Real WiFi 6 driver scan */
		/* Uses management frame scanning via firmware */
		return s_fullmac->ops->start_scan(s_fullmac, NULL);
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_scan_result(fl_net_wifi_scan_entry_t *entries,
				    size_t cap, size_t *count_out)
{
	if (!entries || !count_out || cap == 0)
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		/* Phase 1: Parse scan results from coprocessor */
		/* TODO: Retrieve scan results from AT+CWLAP response buffer */
		*count_out = 0;
		return FL_RESULT_OK;
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* Phase 4: Get scan results from firmware */
		uint16_t count = (uint16_t)cap;
		return s_fullmac->ops->get_scan_results(s_fullmac, entries, &count);
	}

	*count_out = 0;
	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_connect(const fl_net_wifi_cred_t *cred,
				unsigned timeout_ms)
{
	if (!cred || !cred->ssid[0])
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		/* Phase 1-3: ESP32 coprocessor + supplicant
		 * Steps:
		 * 1. Send SSID/auth mode to coprocessor
		 * 2. Coprocessor performs scanning
		 * 3. Coprocessor handles 4-way handshake (WPA2) or SAE (WPA3)
		 * 4. Supplicant bridges to P3 crypto for key derivation
		 */
		uint8_t auth_mode = cred->auth_mode ?
				    cred->auth_mode : FL_WIFI_AUTH_WPA2_PSK;

		wifi_network_t net = {0};
		strncpy(net.ssid, cred->ssid, sizeof(net.ssid) - 1);
		net.auth_type = (cred->passphrase[0] || auth_mode == FL_WIFI_AUTH_WPA2_PSK ||
				  auth_mode == FL_WIFI_AUTH_WPA3_SAE) ?
				 WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

		wifi_join_params_t params = {0};
		params.network = net;
		strncpy(params.password, cred->passphrase,
			sizeof(params.password) - 1);
		params.timeout_ms = timeout_ms;

		return wifi_coproc_join(s_coprocessor, &params);
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* Phase 4: Real WiFi 6 driver
		 * Firmware handles scanning, auth, association, key exchange
		 * Driver bridge coordinates with supplicant for crypto
		 */
		/* TODO: Coordinate with Phase 3 supplicant for 4-way/SAE */
		return FL_RESULT_NOSYS;
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_disconnect(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		return wifi_coproc_disconnect(s_coprocessor);
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		return s_fullmac->ops->deauthenticate(s_fullmac, 1);
	}

	return FL_RESULT_NOSYS;
}

fl_net_wifi_state_t wifi_driver_state(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		wifi_coproc_status_t status = wifi_coproc_status(s_coprocessor);
		switch (status) {
		case WIFI_COPROC_STATUS_DOWN:
			return FL_WIFI_STATE_IDLE;
		case WIFI_COPROC_STATUS_INITIALIZING:
			return FL_WIFI_STATE_IDLE;
		case WIFI_COPROC_STATUS_FIRMWARE_READY:
			return FL_WIFI_STATE_IDLE;
		case WIFI_COPROC_STATUS_SCANNING:
			return FL_WIFI_STATE_SCANNING;
		case WIFI_COPROC_STATUS_AUTHENTICATING:
			return FL_WIFI_STATE_AUTHING;
		case WIFI_COPROC_STATUS_ASSOCIATING:
			return FL_WIFI_STATE_ASSOC;
		case WIFI_COPROC_STATUS_CONNECTED:
			return FL_WIFI_STATE_CONNECTED;
		case WIFI_COPROC_STATUS_ERROR:
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
		/* Phase 1: Coprocessor may not support full HE capabilities query
		 * Set basic values for 802.11ax compatibility */
		cap_out->max_nss_rx = 2;
		cap_out->max_nss_tx = 2;
		cap_out->supports_ofdma = 0; /* ESP32 typically does not */
		cap_out->supports_mu_mimo = 0;
		cap_out->supports_twt = 0;
		cap_out->supports_6ghz = 0;
		cap_out->channel_width_mhz = 20; /* Default */
		return FL_RESULT_OK;
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* Phase 4: Real WiFi 6 driver provides full HE capabilities */
		if (s_fullmac->ops->get_he_capabilities) {
			wifi_fullmac_he_cap_t fw_cap;
			if (s_fullmac->ops->get_he_capabilities(s_fullmac, &fw_cap)
			    == 0) {
				cap_out->max_nss_rx = (fw_cap.mcs_nss[0] >> 4) & 0x0F;
				cap_out->max_nss_tx = (fw_cap.mcs_nss[1] >> 4) & 0x0F;
				cap_out->supports_ofdma =
					(fw_cap.ofdma_dl_supported ||
					 fw_cap.ofdma_ul_supported) ? 1 : 0;
				cap_out->supports_mu_mimo = 1;
				cap_out->supports_twt = 1;
				cap_out->supports_6ghz = 0; /* Check PHY cap */
				cap_out->bss_color = 0;
				cap_out->channel_width_mhz = 80; /* Typical 802.11ax */
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
		/* Phase 4: Real WiFi 6 driver TWT negotiation */
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

	/* Phase 1: Coprocessor TWT not supported */
	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_twt_teardown(uint8_t flow_id)
{
	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		return s_fullmac->ops->teardown_twt(s_fullmac, flow_id);
	}

	return FL_RESULT_NOSYS;
}

fl_net_driver_t *wifi_driver_netdev(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		/* Phase 1: Return coprocessor's netdev (registered on init) */
		return s_coprocessor->netdev;
	}

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac) {
		/* Phase 4: Return FullMAC driver's netdev */
		return s_fullmac->netdev;
	}

	return NULL;
}
