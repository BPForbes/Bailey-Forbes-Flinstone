/*
 * TWT Individual Setup/Teardown Action frames over mgmt transport (#328).
 */

#include "wifi_twt_ota.h"

#include "kernel/core/net/net_wifi_mgmt.h"

#include <string.h>

static int twt_ota_tx_mgmt(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	if (!tr || !tr->tx_mgmt)
		return -1;
	return tr->tx_mgmt(tr, frame, len);
}

static int twt_ota_rx_mgmt(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out)
{
	if (!tr || !tr->rx_mgmt)
		return -1;
	return tr->rx_mgmt(tr, frame, cap, len_out, WIFI_TWT_OTA_TIMEOUT_MS);
}

int wifi_twt_ota_setup(const uint8_t sta_mac[6], const uint8_t bssid[6],
		       const fl_net_wifi_twt_params_t *req, fl_net_wifi_twt_params_t *agreed_out,
		       wifi_mgmt_transport_t *tr)
{
	uint8_t tx[WIFI_OTA_FRAME_MAX];
	uint8_t rx[WIFI_OTA_FRAME_MAX];
	size_t tx_len = 0;
	size_t rx_len = 0;
	static uint8_t s_dialog = 1u;

	if (!sta_mac || !bssid || !req || !agreed_out || !tr)
		return -1;
	if (fl_net_wifi_mgmt_build_twt_setup_req(sta_mac, bssid, s_dialog, req, tx, sizeof(tx),
						 &tx_len) != FL_RESULT_OK)
		return -1;
	if (twt_ota_tx_mgmt(tr, tx, tx_len) != 0)
		return -1;
	if (twt_ota_rx_mgmt(tr, rx, sizeof(rx), &rx_len) != 0)
		return -1;
	if (fl_net_wifi_mgmt_parse_twt_setup_resp(rx, rx_len, agreed_out) != FL_RESULT_OK)
		return -1;
	s_dialog++;
	return 0;
}

int wifi_twt_ota_teardown(const uint8_t sta_mac[6], const uint8_t bssid[6], uint8_t flow_id,
			  wifi_mgmt_transport_t *tr)
{
	uint8_t tx[WIFI_OTA_FRAME_MAX];
	size_t tx_len = 0;
	static uint8_t s_dialog = 1u;

	if (!sta_mac || !bssid || !tr)
		return -1;
	if (fl_net_wifi_mgmt_build_twt_teardown(sta_mac, bssid, s_dialog, flow_id, tx, sizeof(tx),
						&tx_len) != FL_RESULT_OK)
		return -1;
	s_dialog++;
	return twt_ota_tx_mgmt(tr, tx, tx_len);
}
