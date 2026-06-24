/*
 * Mock management + EAPOL data transport for in-tree Wi-Fi OTA (#328).
 * Driver execution: loopback AP answers auth/assoc/SAE/EAPOL over the air.
 */

#include "wifi_mgmt_transport.h"

#include "kernel/core/net/net_wifi_mgmt.h"
#include "kernel/core/net/net_wire.h"

#include <string.h>

#define WIFI_OTA_ETH_P_EAPOL 0x888eu

static int mock_enqueue(uint8_t queue[][WIFI_OTA_FRAME_MAX], size_t *lens, unsigned *count,
			unsigned cap, const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!frame || len == 0 || len > WIFI_OTA_FRAME_MAX || *count >= cap)
		return -1;
	idx = *count;
	memcpy(queue[idx], frame, len);
	lens[idx] = len;
	(*count)++;
	return 0;
}

static int mock_dequeue(uint8_t queue[][WIFI_OTA_FRAME_MAX], size_t *lens, unsigned *count,
			uint8_t *frame, size_t cap, size_t *len_out)
{
	if (*count == 0)
		return -1;
	if (cap < lens[0])
		return -1;
	memcpy(frame, queue[0], lens[0]);
	*len_out = lens[0];
	if (*count > 1u) {
		memmove(queue[0], queue[1], (*count - 1u) * WIFI_OTA_FRAME_MAX);
		memmove(&lens[0], &lens[1], (*count - 1u) * sizeof(lens[0]));
	}
	(*count)--;
	return 0;
}

static void mock_ap_eapol_responses(wifi_mgmt_transport_mock_ctx_t *ctx)
{
	uint8_t msg1[64];
	uint8_t msg3[64];
	uint8_t eth[WIFI_OTA_FRAME_MAX];
	const uint8_t *ap = ctx->cfg.ap ? ctx->cfg.ap->bssid : NULL;
	const uint8_t *sta = ctx->cfg.sta_mac;

	if (!ap || !sta)
		return;

	memset(msg1, 0, sizeof(msg1));
	msg1[0] = 0x01;
	{
		size_t pos = 0;

		memcpy(eth + pos, sta, 6u);
		pos += 6u;
		memcpy(eth + pos, ap, 6u);
		pos += 6u;
		eth[pos++] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL >> 8);
		eth[pos++] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL & 0xffu);
		memcpy(eth + pos, msg1, 40u);
		pos += 40u;
		(void)mock_enqueue(ctx->data_rx, ctx->data_rx_len, &ctx->data_rx_count,
				   WIFI_OTA_DATA_Q, eth, pos);
	}

	memset(msg3, 0, sizeof(msg3));
	msg3[0] = 0x03;
	{
		size_t pos = 0;

		memcpy(eth + pos, sta, 6u);
		pos += 6u;
		memcpy(eth + pos, ap, 6u);
		pos += 6u;
		eth[pos++] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL >> 8);
		eth[pos++] = (uint8_t)(WIFI_OTA_ETH_P_EAPOL & 0xffu);
		memcpy(eth + pos, msg3, 38u);
		pos += 38u;
		(void)mock_enqueue(ctx->data_rx, ctx->data_rx_len, &ctx->data_rx_count,
				   WIFI_OTA_DATA_Q, eth, pos);
	}
	ctx->eapol_rx_pending = 0;
}

static void mock_ap_handle_mgmt(wifi_mgmt_transport_mock_ctx_t *ctx, const uint8_t *frame,
				size_t len)
{
	const wifi_network_t *ap = ctx->cfg.ap;
	const uint8_t *sta = ctx->cfg.sta_mac;
	uint8_t resp[WIFI_OTA_FRAME_MAX];
	size_t resp_len = 0;
	uint8_t subtype;
	uint16_t auth_alg;

	if (!ap || !sta || len < FL_WIFI_MGMT_HDR_LEN)
		return;

	subtype = (uint8_t)(frame[0] & 0xfcu);
	if (subtype == 0xb0u && len >= FL_WIFI_MGMT_HDR_LEN + 6u) {
		auth_alg = (uint16_t)frame[24] | ((uint16_t)frame[25] << 8);
		if (auth_alg == 3u && ap->auth_mode == WIFI_AUTH_WPA3_SAE) {
			static const uint8_t k_confirm[] = "confirm";

			if (fl_net_wifi_mgmt_build_sae_auth(ap->bssid, sta, 2u, k_confirm,
							    sizeof(k_confirm) - 1u, resp,
							    sizeof(resp),
							    &resp_len) == FL_RESULT_OK)
				(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len,
						   &ctx->mgmt_rx_count, WIFI_OTA_MGMT_Q, resp,
						   resp_len);
			return;
		}
		if (fl_net_wifi_mgmt_build_auth_resp(ap->bssid, sta, 2u, resp, sizeof(resp),
						     &resp_len) == FL_RESULT_OK)
			(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count,
					  WIFI_OTA_MGMT_Q, resp, resp_len);
		return;
	}

	if (subtype == 0x00u &&
	    fl_net_wifi_mgmt_build_assoc_resp(ap->bssid, sta, resp, sizeof(resp),
					      &resp_len) == FL_RESULT_OK)
		(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count,
				  WIFI_OTA_MGMT_Q, resp, resp_len);
}

static void mock_ap_handle_data(wifi_mgmt_transport_mock_ctx_t *ctx, const uint8_t *frame,
				size_t len)
{
	uint16_t ethertype;

	if (len < FL_NET_ETH_FRAME_HDR_LEN + 1u)
		return;
	ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | (uint16_t)frame[13]);
	if (ethertype != WIFI_OTA_ETH_P_EAPOL)
		return;
	if (frame[FL_NET_ETH_FRAME_HDR_LEN] == 0x02u && !ctx->eapol_rx_pending) {
		ctx->eapol_rx_pending = 1;
		mock_ap_eapol_responses(ctx);
	}
}

static int mock_tx_mgmt(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	if (!ctx || !frame || len == 0)
		return -1;
	mock_ap_handle_mgmt(ctx, frame, len);
	return 0;
}

static int mock_rx_mgmt(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
			unsigned timeout_ms)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	(void)timeout_ms;
	if (!ctx || !frame || !len_out)
		return -1;
	if (mock_dequeue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count, frame, cap,
			 len_out) != 0)
		return -1;
	return 0;
}

static int mock_tx_data(wifi_mgmt_transport_t *tr, const uint8_t *frame, size_t len)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	if (!ctx || !frame || len == 0)
		return -1;
	mock_ap_handle_data(ctx, frame, len);
	return 0;
}

static int mock_rx_data(wifi_mgmt_transport_t *tr, uint8_t *frame, size_t cap, size_t *len_out,
			unsigned timeout_ms)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	(void)timeout_ms;
	if (!ctx || !frame || !len_out)
		return -1;
	if (mock_dequeue(ctx->data_rx, ctx->data_rx_len, &ctx->data_rx_count, frame, cap,
			 len_out) != 0)
		return -1;
	return 0;
}

int wifi_mgmt_transport_mock_init(wifi_mgmt_transport_t *tr, void *ctx_storage,
				  const wifi_mgmt_transport_mock_cfg_t *cfg)
{
	wifi_mgmt_transport_mock_ctx_t *ctx;

	if (!tr || !ctx_storage || !cfg || !cfg->ap || !cfg->sta_mac)
		return -1;
	ctx = (wifi_mgmt_transport_mock_ctx_t *)ctx_storage;
	memset(ctx, 0, sizeof(*ctx));
	ctx->cfg = *cfg;
	tr->ctx = ctx;
	tr->tx_mgmt = mock_tx_mgmt;
	tr->rx_mgmt = mock_rx_mgmt;
	tr->tx_data = mock_tx_data;
	tr->rx_data = mock_rx_data;
	return 0;
}

void wifi_mgmt_transport_mock_reset(wifi_mgmt_transport_t *tr)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	if (!ctx)
		return;
	ctx->mgmt_rx_count = 0;
	ctx->data_rx_count = 0;
	ctx->eapol_rx_pending = 0;
}

size_t wifi_mgmt_transport_mock_ctx_size(void)
{
	return sizeof(wifi_mgmt_transport_mock_ctx_t);
}
