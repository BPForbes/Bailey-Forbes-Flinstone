/*
 * WiFi Driver Backend Implementation
 * Bridges v4.3.0 drivers to contract_p3_wifi.h
 * Routes to Phase 1 (coprocessor) or Phase 4 (FullMAC) when available
 * Falls back to lab simulation when no hardware present
 */

#include "wifi_driver_backend.h"
#include "wifi_lab_backend.h"
#include "wifi_lab_router.h"
#include "wifi_coprocessor.h"
#include "wifi_fullmac.h"
#include "wifi_fullmac_hw_internal.h"
#include "net_wifi_fullmac.h"
#include "net_wifi_nl80211.h"
#include "wifi_uart_transport.h"
#include "wifi_mgmt_transport_nl80211.h"
#include "wifi_connect_ota.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

static wifi_backend_type_t s_backend_type = WIFI_BACKEND_NONE;
static wifi_coproc_t *s_coprocessor = NULL;
static wifi_fullmac_t *s_fullmac = NULL;
static int s_lab_dhcp_route;

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

static uint32_t wifi_channel_to_freq_mhz(uint8_t channel, uint8_t band)
{
	if (channel == 0u)
		return 0u;
	if (band == FL_WIFI_BAND_6GHZ)
		return 5955u + 5u * (uint32_t)(channel >= 1u ? (channel - 1u) : 0u);
	if (band == FL_WIFI_BAND_5GHZ || channel >= 32u)
		return 5000u + 5u * (uint32_t)channel;
	if (channel == 14u)
		return 2484u;
	if (channel <= 13u)
		return 2407u + 5u * (uint32_t)channel;
	return 0u;
}

static void wifi_network_to_scan_entry(const wifi_network_t *src,
				       fl_net_wifi_scan_entry_t *dst)
{
	memset(dst, 0, sizeof(*dst));
	strncpy(dst->ssid, src->ssid, sizeof(dst->ssid) - 1u);
	dst->ssid[sizeof(dst->ssid) - 1u] = '\0';
	memcpy(dst->bssid, src->bssid, sizeof(dst->bssid));
	dst->rssi_dbm = src->rssi;
	dst->channel = src->channel;
	dst->band = FL_WIFI_BAND_ANY;
	dst->channel_width_mhz = 20;
	switch (src->auth_mode) {
	case WIFI_AUTH_OPEN:
		dst->auth_mode = FL_WIFI_AUTH_OPEN;
		break;
	case WIFI_AUTH_WPA3_SAE:
		dst->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
		break;
	default:
		dst->auth_mode = FL_WIFI_AUTH_WPA2_PSK;
		break;
	}
}

static void wifi_scan_entry_to_network(const fl_net_wifi_scan_entry_t *src, wifi_network_t *dst)
{
	memset(dst, 0, sizeof(*dst));
	strncpy(dst->ssid, src->ssid, sizeof(dst->ssid) - 1u);
	dst->ssid[sizeof(dst->ssid) - 1u] = '\0';
	memcpy(dst->bssid, src->bssid, sizeof(dst->bssid));
	dst->rssi = (int8_t)src->rssi_dbm;
	dst->channel = src->channel;
	dst->freq = src->channel ? wifi_channel_to_freq_mhz(src->channel, src->band) : 0u;
	switch (src->auth_mode) {
	case FL_WIFI_AUTH_OPEN:
		dst->auth_mode = WIFI_AUTH_OPEN;
		break;
	case FL_WIFI_AUTH_WPA3_SAE:
		dst->auth_mode = WIFI_AUTH_WPA3_SAE;
		break;
	default:
		dst->auth_mode = WIFI_AUTH_WPA2_PSK;
		break;
	}
}

static int wifi_env_truthy(const char *env)
{
	return env && env[0] && strcmp(env, "0") != 0;
}

static fl_result_t wifi_env_parse_int(const char *env, int *out)
{
	char *end = NULL;
	long v;

	if (!env || !env[0] || !out)
		return FL_RESULT_INVAL;

	errno = 0;
	v = strtol(env, &end, 10);
	if (errno != 0 || end == env || (end && *end != '\0') || v < 0 || v > INT_MAX)
		return FL_RESULT_INVAL;

	*out = (int)v;
	return FL_RESULT_OK;
}

static fl_result_t wifi_backend_try_uart_coprocessor(void)
{
	const char *uart_fd_env = getenv("FL_WIFI_UART_FD");
	const char *baud_env;
	int uart_fd;
	wifi_uart_baud_t baud = WIFI_UART_BAUD_115200;
	fl_result_t parse_rc;

	if (!uart_fd_env || !uart_fd_env[0])
		return FL_RESULT_NOSYS;

	parse_rc = wifi_env_parse_int(uart_fd_env, &uart_fd);
	if (parse_rc != FL_RESULT_OK || uart_fd < 0)
		return FL_RESULT_NOSYS;

	baud_env = getenv("FL_WIFI_UART_BAUD");
	if (baud_env && baud_env[0]) {
		int baud_val;

		if (wifi_env_parse_int(baud_env, &baud_val) == FL_RESULT_OK)
			baud = (wifi_uart_baud_t)baud_val;
	}

	if (wifi_uart_coproc_create("wlan0", uart_fd, baud, &s_coprocessor) != 0)
		return FL_RESULT_NOSYS;

	if (wifi_coproc_init(s_coprocessor) != 0) {
		wifi_coproc_destroy(s_coprocessor);
		s_coprocessor = NULL;
		return FL_RESULT_NOSYS;
	}

	s_backend_type = WIFI_BACKEND_COPROCESSOR;
	return FL_RESULT_OK;
}

static fl_result_t wifi_backend_try_ax_mock(void)
{
	const char *env = getenv("FL_WIFI_80211AX_MOCK");

	if (!env || !env[0] || strcmp(env, "0") == 0)
		return FL_RESULT_NOSYS;

	if (wifi_lab_mock_attach(&s_fullmac) != 0)
		return FL_RESULT_NOSYS;

	s_backend_type = WIFI_BACKEND_QEMU;
	return FL_RESULT_OK;
}

static fl_result_t wifi_backend_try_nl80211_fullmac(void)
{
	if (!fl_net_wifi_fullmac_available())
		return FL_RESULT_NOSYS;
	if (fl_net_wifi_fullmac_init(NULL) != FL_RESULT_OK)
		return FL_RESULT_NOSYS;
	s_backend_type = WIFI_BACKEND_NL80211;
	return FL_RESULT_OK;
}

static fl_result_t wifi_backend_try_fullmac_hw(void)
{
	if (wifi_fullmac_hw_probe(&s_fullmac, NULL) != 0)
		return FL_RESULT_NOSYS;
	s_backend_type = WIFI_BACKEND_FULLMAC;
	return FL_RESULT_OK;
}

fl_result_t wifi_driver_backend_init(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		wifi_coproc_destroy(s_coprocessor);
		s_coprocessor = NULL;
	}
	if (s_fullmac && s_backend_type == WIFI_BACKEND_QEMU)
		wifi_lab_mock_detach(s_fullmac);
	if (s_fullmac && s_backend_type == WIFI_BACKEND_FULLMAC)
		wifi_fullmac_hw_detach(s_fullmac);
	if (s_backend_type == WIFI_BACKEND_NL80211)
		fl_net_wifi_fullmac_deinit();

	s_backend_type = WIFI_BACKEND_NONE;
	s_coprocessor = NULL;
	s_fullmac = NULL;
	s_lab_dhcp_route = 0;

	if (wifi_backend_try_uart_coprocessor() == FL_RESULT_OK)
		return FL_RESULT_OK;

	if (wifi_backend_try_nl80211_fullmac() == FL_RESULT_OK)
		return FL_RESULT_OK;

	if (wifi_backend_try_ax_mock() == FL_RESULT_OK)
		return FL_RESULT_OK;

	if (wifi_backend_try_fullmac_hw() == FL_RESULT_OK)
		return FL_RESULT_OK;

	/* Phase 4 FullMAC WiFi 6 hardware: probe when PCIe enumeration lands. */
	return FL_RESULT_NOSYS;
}

wifi_backend_type_t wifi_driver_backend_active(void)
{
	return s_backend_type;
}

int wifi_driver_backend_is_physical(void)
{
	return s_backend_type == WIFI_BACKEND_NL80211 && fl_net_wifi_fullmac_is_physical();
}

int wifi_driver_lab_sim_enabled(void)
{
	return wifi_env_truthy(getenv("FL_NET_WIFI_LAB")) ||
	       wifi_env_truthy(getenv("FL_NET_WIFI_FULLMAC_LAB"));
}

static fl_result_t wifi_driver_nl80211_connect(const fl_net_wifi_cred_t *cred,
					       unsigned timeout_ms)
{
	fl_net_wifi_nl80211_t *nl;
	fl_net_wifi_scan_entry_t scan[32];
	size_t n = 0;
	wifi_network_t ap;
	wifi_mgmt_transport_t tr;
	uint8_t sta_mac[6];
	size_t i;
	fl_result_t rc;

	(void)timeout_ms;
	if (!cred || !cred->ssid[0])
		return FL_RESULT_INVAL;
	if (!fl_net_wifi_fullmac_is_physical())
		return FL_RESULT_NOSYS;

	nl = fl_net_wifi_fullmac_nl80211();
	if (!nl)
		return FL_RESULT_NOSYS;
	if (fl_net_wifi_fullmac_sta_mac(sta_mac) != FL_RESULT_OK)
		return FL_RESULT_ERR;

	rc = fl_net_wifi_fullmac_scan(FL_WIFI_BAND_ANY, cred->ssid, 5000u);
	if (rc != FL_RESULT_OK)
		return rc;
	rc = fl_net_wifi_fullmac_scan_result(scan, 32, &n);
	if (rc != FL_RESULT_OK || n == 0u)
		return FL_RESULT_ERR;

	memset(&ap, 0, sizeof(ap));
	for (i = 0; i < n; i++) {
		if (strcmp(scan[i].ssid, cred->ssid) != 0)
			continue;
		if (cred->bssid[0] | cred->bssid[1] | cred->bssid[2] | cred->bssid[3] |
		    cred->bssid[4] | cred->bssid[5]) {
			if (memcmp(scan[i].bssid, cred->bssid, 6) != 0)
				continue;
		}
		wifi_scan_entry_to_network(&scan[i], &ap);
		break;
	}
	if (!ap.ssid[0])
		return FL_RESULT_ERR;

	if (wifi_mgmt_transport_nl80211_init(&tr, nl) != 0)
		return FL_RESULT_ERR;
	{
		uint32_t freq = ap.freq;

		if (freq == 0u)
			freq = wifi_channel_to_freq_mhz(ap.channel, FL_WIFI_BAND_ANY);
		(void)fl_net_wifi_nl80211_set_mgmt_freq(nl, freq);
	}
	if (wifi_connect_ota_run(cred, &ap, sta_mac, &tr, NULL) != 0)
		return FL_RESULT_ERR;
	return FL_RESULT_OK;
}

fl_result_t wifi_driver_scan(uint8_t band, unsigned timeout_ms)
{
	(void)band;
	(void)timeout_ms;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return wifi_int_to_result(wifi_coproc_scan(s_coprocessor));

	if (s_backend_type == WIFI_BACKEND_NL80211) {
		if (fl_net_wifi_fullmac_is_lab())
			return wifi_driver_lab_scan(band, timeout_ms);
		return fl_net_wifi_fullmac_scan(band, NULL, timeout_ms);
	}

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops)
		return wifi_int_to_result(s_fullmac->ops->start_scan(s_fullmac, NULL));

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_scan_result(fl_net_wifi_scan_entry_t *entries,
				    size_t cap, size_t *count_out)
{
	if (!entries || !count_out || cap == 0)
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor) {
		wifi_network_t networks[32];
		uint16_t count = (uint16_t)(cap < 32u ? cap : 32u);
		size_t i;

		if (wifi_coproc_get_scan_results(s_coprocessor, networks, &count) != 0) {
			*count_out = 0;
			return FL_RESULT_NOSYS;
		}

		*count_out = 0;
		for (i = 0; i < (size_t)count && i < cap; i++) {
			wifi_network_to_scan_entry(&networks[i], &entries[i]);
			(*count_out)++;
		}
		return FL_RESULT_OK;
	}

	if (s_backend_type == WIFI_BACKEND_NL80211) {
		if (fl_net_wifi_fullmac_is_lab())
			return wifi_driver_lab_scan_result(entries, cap, count_out);
		return fl_net_wifi_fullmac_scan_result(entries, cap, count_out);
	}

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops) {
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
			wifi_network_to_scan_entry(&networks[i], &entries[i]);
			if (s_backend_type == WIFI_BACKEND_QEMU)
				wifi_lab_mock_enrich_scan_entry(i, &entries[i]);
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

	if (s_backend_type == WIFI_BACKEND_QEMU && s_fullmac)
		return wifi_int_to_result(wifi_lab_mock_connect(s_fullmac, cred));

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac)
		return wifi_int_to_result(wifi_fullmac_station_connect(s_fullmac, cred));

	if (s_backend_type == WIFI_BACKEND_NL80211) {
		if (fl_net_wifi_fullmac_is_lab())
			return wifi_driver_lab_connect(cred, NULL, NULL);
		return wifi_driver_nl80211_connect(cred, timeout_ms);
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_disconnect(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return wifi_int_to_result(wifi_coproc_disconnect(s_coprocessor));

	if (s_backend_type == WIFI_BACKEND_NL80211)
		return fl_net_wifi_fullmac_disconnect();

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops)
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

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops) {
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

	if (s_backend_type == WIFI_BACKEND_QEMU && s_fullmac &&
	    s_fullmac->state == WIFI_FULLMAC_STATE_CONNECTED) {
		wifi_lab_mock_fill_he_cap(cap_out);
		return FL_RESULT_OK;
	}

	if (s_backend_type == WIFI_BACKEND_NL80211) {
		if (fl_net_wifi_fullmac_is_lab())
			return wifi_driver_lab_he_cap(cap_out);
		return fl_net_wifi_fullmac_he_cap(cap_out);
	}

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops) {
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
				cap_out->channel_width_mhz = 80u;
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

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops) {
		wifi_fullmac_twt_setup_t fw_req = {
			.flow_id = req->flow_id,
			.wake_interval_ms = req->wake_interval_us / 1000,
			.wake_duration_us = req->wake_duration_us,
			.requestor = 1,
			.trigger = req->trigger_enabled,
		};
		if (s_fullmac->ops->setup_twt(s_fullmac, &fw_req) == 0) {
			memcpy(agreed_out, req, sizeof(*agreed_out));
			agreed_out->flow_id = fw_req.flow_id;
			return FL_RESULT_OK;
		}
	}

	return FL_RESULT_NOSYS;
}

fl_result_t wifi_driver_twt_teardown(uint8_t flow_id)
{
	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac && s_fullmac->ops)
		return wifi_int_to_result(s_fullmac->ops->teardown_twt(s_fullmac, flow_id));

	return FL_RESULT_NOSYS;
}

fl_net_driver_t *wifi_driver_netdev(void)
{
	if (s_backend_type == WIFI_BACKEND_COPROCESSOR && s_coprocessor)
		return s_coprocessor->netdev;

	if (s_backend_type == WIFI_BACKEND_NL80211)
		return fl_net_wifi_fullmac_driver();

	if ((s_backend_type == WIFI_BACKEND_FULLMAC || s_backend_type == WIFI_BACKEND_QEMU) &&
	    s_fullmac)
		return s_fullmac->netdev;

	return NULL;
}

void wifi_driver_lab_dhcp_route_enable(int on)
{
	s_lab_dhcp_route = on ? 1 : 0;
}

fl_result_t wifi_driver_dhcp_exchange(const uint8_t cli_mac[6], const uint8_t *req,
				      size_t req_len, uint8_t *reply, size_t reply_cap,
				      size_t *reply_len)
{
	if (!cli_mac || !req || !reply || !reply_len)
		return FL_RESULT_INVAL;

	if (s_backend_type == WIFI_BACKEND_QEMU)
		return wifi_lab_router_dhcp_exchange(cli_mac, req, req_len, reply, reply_cap,
						     reply_len);

	if (s_backend_type == WIFI_BACKEND_FULLMAC && s_fullmac &&
	    wifi_fullmac_hw_ota_sim_active(s_fullmac) &&
	    s_fullmac->state == WIFI_FULLMAC_STATE_CONNECTED)
		return wifi_lab_router_dhcp_exchange(cli_mac, req, req_len, reply, reply_cap,
						     reply_len);

	if (!s_lab_dhcp_route)
		return FL_RESULT_NOSYS;

	return wifi_lab_router_dhcp_exchange(cli_mac, req, req_len, reply, reply_cap,
					     reply_len);
}

fl_result_t wifi_driver_lab_scan(uint8_t band, unsigned timeout_ms)
{
	return wifi_lab_scan(band, timeout_ms);
}

fl_result_t wifi_driver_lab_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
					size_t *count_out)
{
	return wifi_lab_scan_result(entries, cap, count_out);
}

fl_result_t wifi_driver_lab_connect(const fl_net_wifi_cred_t *cred,
				    fl_net_wifi_scan_entry_t *ap_out,
				    fl_net_wifi_he_cap_t *he_out)
{
	return wifi_lab_connect(cred, ap_out, he_out);
}

fl_result_t wifi_driver_lab_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
	return wifi_lab_he_cap(cap_out);
}

void wifi_driver_lab_reset(void)
{
	wifi_lab_reset();
}
