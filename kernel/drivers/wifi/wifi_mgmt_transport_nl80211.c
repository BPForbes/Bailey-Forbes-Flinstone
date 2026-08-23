/*
 * nl80211 + AF_PACKET transport for production Wi-Fi OTA (#328).
 * Driver execution: mgmt via nl80211, EAPOL/data via FullMAC netdev.
 */

#include "wifi_mgmt_transport_nl80211.h"

#include "net_wifi_fullmac.h"
#include "wifi_nl80211.h"

typedef struct {
	fl_net_wifi_nl80211_t *nl;
} wifi_mgmt_transport_nl80211_ctx_t;

static wifi_mgmt_transport_nl80211_ctx_t s_nl80211_tr;

static int nl80211_tr_tx_mgmt(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	wifi_mgmt_transport_nl80211_ctx_t *ctx = tr ? (wifi_mgmt_transport_nl80211_ctx_t *)tr->ctx : NULL;

	if (!ctx || !ctx->nl || !frame || len == 0u)
		return -1;
	return fl_net_wifi_nl80211_mgmt_tx(ctx->nl, frame, len, 5000u) == FL_RESULT_OK ? 0 : -1;
}

static int nl80211_tr_rx_mgmt(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
			      unsigned timeout_ms)
{
	wifi_mgmt_transport_nl80211_ctx_t *ctx = tr ? (wifi_mgmt_transport_nl80211_ctx_t *)tr->ctx : NULL;

	if (!ctx || !ctx->nl || !frame || !len_out)
		return -1;
	(void)fl_net_wifi_nl80211_poll(ctx->nl, timeout_ms ? timeout_ms : 5000u);
	return fl_net_wifi_nl80211_mgmt_rx(ctx->nl, frame, cap, len_out,
					   timeout_ms ? timeout_ms : 5000u) == FL_RESULT_OK
		       ? 0
		       : -1;
}

static int nl80211_tr_tx_data(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	fl_net_driver_t *drv;
	fl_net_frame_view_t view;

	(void)tr;
	drv = fl_net_wifi_fullmac_driver();
	if (!drv || !drv->send || !frame || len == 0u)
		return -1;
	view.data = frame;
	view.len = len;
	return drv->send(drv, &view) == FL_RESULT_OK ? 0 : -1;
}

static int nl80211_tr_rx_data(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
			      unsigned timeout_ms)
{
	fl_net_driver_t *drv;
	fl_net_frame_mut_t out;

	(void)tr;
	(void)timeout_ms;
	drv = fl_net_wifi_fullmac_driver();
	if (!drv || !drv->recv || !frame || !len_out)
		return -1;
	out.data = frame;
	out.cap = cap;
	out.len = 0u;
	if (drv->recv(drv, &out) != FL_RESULT_OK)
		return -1;
	*len_out = out.len;
	return 0;
}

int wifi_mgmt_transport_nl80211_init(wifi_mgmt_transport_t *tr, fl_net_wifi_nl80211_t *nl)
{
	if (!tr || !nl)
		return -1;
	s_nl80211_tr.nl = nl;
	tr->ctx = &s_nl80211_tr;
	tr->tx_mgmt = nl80211_tr_tx_mgmt;
	tr->rx_mgmt = nl80211_tr_rx_mgmt;
	tr->tx_data = nl80211_tr_tx_data;
	tr->rx_data = nl80211_tr_rx_data;
	return 0;
}
