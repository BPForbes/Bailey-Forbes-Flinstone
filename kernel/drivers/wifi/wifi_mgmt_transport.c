/*
 * Mock management + EAPOL data transport for in-tree Wi-Fi OTA (#328).
 * Driver execution: loopback AP answers auth/assoc/SAE/EAPOL over the air.
 */

#include "wifi_mgmt_transport.h"

#include "kernel/core/net/net_wifi_mgmt.h"
#include "kernel/core/net/net_wifi_sae.h"
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

static int mock_sae_init_ap(wifi_mgmt_transport_mock_ctx_t *ctx)
{
	const wifi_network_t *ap = ctx->cfg.ap;
	const uint8_t *sta = ctx->cfg.sta_mac;
	const char *pass = ctx->cfg.passphrase;

	if (!ap || !sta || !pass || !pass[0])
		return -1;
	if (ctx->sae_ap)
		return 0;
	if (fl_net_wifi_sae_dragonfly_ctx_create(&ctx->sae_ap) != FL_RESULT_OK)
		return -1;
	if (fl_net_wifi_sae_dragonfly_init_ap(ctx->sae_ap, ap->ssid, pass, ap->bssid, sta) !=
	    FL_RESULT_OK) {
		fl_net_wifi_sae_dragonfly_ctx_destroy(ctx->sae_ap);
		ctx->sae_ap = NULL;
		return -1;
	}
	return 0;
}

static void mock_sae_reset(wifi_mgmt_transport_mock_ctx_t *ctx)
{
	if (ctx->sae_ap) {
		fl_net_wifi_sae_dragonfly_ctx_destroy(ctx->sae_ap);
		ctx->sae_ap = NULL;
	}
	ctx->sae_clog_sent = 0;
	ctx->sae_ap_commit_sent = 0;
}

static void mock_ap_handle_sae_auth(wifi_mgmt_transport_mock_ctx_t *ctx, const uint8_t *frame,
				    size_t len, uint16_t auth_seq, const uint8_t *body,
				    size_t body_len)
{
	const wifi_network_t *ap = ctx->cfg.ap;
	const uint8_t *sta = ctx->cfg.sta_mac;
	uint8_t resp[WIFI_OTA_FRAME_MAX];
	size_t resp_len = 0;
	static const uint8_t k_clog_token[] = { 'c', 'l', 'o', 'g' };
	int has_clog = 0;

	(void)frame;
	(void)len;
	if (!ap || !sta)
		return;

	if (body_len >= FL_NET_WIFI_SAE_COMMIT_BODY_LEN + sizeof(k_clog_token) &&
	    memcmp(body + 2u, k_clog_token, sizeof(k_clog_token)) == 0)
		has_clog = 1;

	if (auth_seq == 1u && !has_clog && !ctx->sae_clog_sent) {
		ctx->sae_clog_sent = 1u;
		if (fl_net_wifi_mgmt_build_sae_auth(ap->bssid, sta, 2u, k_clog_token,
						    sizeof(k_clog_token), resp, sizeof(resp),
						    &resp_len) == FL_RESULT_OK) {
			resp[28] = (uint8_t)(FL_WIFI_SAE_STATUS_ANTICLOGGING & 0xffu);
			resp[29] = (uint8_t)((FL_WIFI_SAE_STATUS_ANTICLOGGING >> 8) & 0xffu);
			(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count,
					   WIFI_OTA_MGMT_Q, resp, resp_len);
		}
		return;
	}

	if (auth_seq == 1u && !has_clog)
		return;

	if (auth_seq == 1u) {
		uint8_t commit_body[128];
		size_t commit_len = 0;

		if (mock_sae_init_ap(ctx) != 0)
			return;
		if (fl_net_wifi_sae_dragonfly_build_commit(ctx->sae_ap, NULL, 0u, commit_body,
							   sizeof(commit_body),
							   &commit_len) != FL_RESULT_OK)
			return;
		if (fl_net_wifi_sae_dragonfly_rx_commit(ctx->sae_ap, body, body_len) != FL_RESULT_OK)
			return;
		if (fl_net_wifi_mgmt_build_sae_auth(ap->bssid, sta, 2u, commit_body, commit_len, resp,
						    sizeof(resp), &resp_len) != FL_RESULT_OK)
			return;
		ctx->sae_ap_commit_sent = 1u;
		(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count,
				   WIFI_OTA_MGMT_Q, resp, resp_len);
		return;
	}

	if (auth_seq == 2u && body_len >= FL_NET_WIFI_SAE_CONFIRM_BODY_LEN) {
		uint8_t confirm_body[64];
		uint8_t pmk[FL_NET_WIFI_PMK_LEN];
		size_t confirm_len = 0;

		if (!ctx->sae_ap || !ctx->sae_ap_commit_sent)
			return;
		if (fl_net_wifi_sae_dragonfly_verify_confirm(ctx->sae_ap, body, body_len, pmk) !=
		    FL_RESULT_OK)
			return;
		if (fl_net_wifi_sae_dragonfly_build_confirm(ctx->sae_ap, confirm_body,
							    sizeof(confirm_body),
							    &confirm_len) != FL_RESULT_OK)
			return;
		if (fl_net_wifi_mgmt_build_sae_auth(ap->bssid, sta, 2u, confirm_body, confirm_len,
						    resp, sizeof(resp), &resp_len) != FL_RESULT_OK)
			return;
		(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len, &ctx->mgmt_rx_count,
				   WIFI_OTA_MGMT_Q, resp, resp_len);
	}
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
	uint16_t auth_seq;

	if (!ap || !sta || len < FL_WIFI_MGMT_HDR_LEN)
		return;

	subtype = (uint8_t)(frame[0] & 0xfcu);
	if (subtype == 0xd0u && len >= FL_WIFI_MGMT_HDR_LEN + 3u) {
		uint8_t action = frame[25];
		uint8_t flow_id;

		if (frame[24] == FL_WIFI_ACTION_CAT_S1G && action == FL_WIFI_ACTION_TWT_SETUP) {
			fl_net_wifi_twt_params_t agreed = {
				.wake_duration_us = 8000u,
				.wake_interval_us = 100000u,
			};

			flow_id = ctx->twt_flow_next++;
			agreed.flow_id = flow_id;
			if (fl_net_wifi_mgmt_build_twt_setup_resp(ap->bssid, sta, frame[26], flow_id,
								  &agreed, resp, sizeof(resp),
								  &resp_len) == FL_RESULT_OK)
				(void)mock_enqueue(ctx->mgmt_rx, ctx->mgmt_rx_len,
						   &ctx->mgmt_rx_count, WIFI_OTA_MGMT_Q, resp,
						   resp_len);
		}
		return;
	}

	if (subtype == 0xb0u && len >= FL_WIFI_MGMT_HDR_LEN + 6u) {
		auth_alg = (uint16_t)frame[24] | ((uint16_t)frame[25] << 8);
		auth_seq = (uint16_t)frame[26] | ((uint16_t)frame[27] << 8);
		if (auth_alg == 3u && ap->auth_mode == WIFI_AUTH_WPA3_SAE) {
			const uint8_t *body = frame + FL_WIFI_MGMT_HDR_LEN + 6u;
			size_t body_len = len - (FL_WIFI_MGMT_HDR_LEN + 6u);

			mock_ap_handle_sae_auth(ctx, frame, len, auth_seq, body, body_len);
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
	mock_sae_reset(ctx);
	memset(ctx, 0, sizeof(*ctx));
	ctx->cfg = *cfg;
	tr->ctx = ctx;
	tr->tx_mgmt = mock_tx_mgmt;
	tr->rx_mgmt = mock_rx_mgmt;
	tr->tx_data = mock_tx_data;
	tr->rx_data = mock_rx_data;
	return 0;
}

void wifi_mgmt_transport_mock_deinit(wifi_mgmt_transport_t *tr)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	if (!ctx)
		return;
	mock_sae_reset(ctx);
	tr->ctx = NULL;
}

void wifi_mgmt_transport_mock_reset(wifi_mgmt_transport_t *tr)
{
	wifi_mgmt_transport_mock_ctx_t *ctx = tr ? (wifi_mgmt_transport_mock_ctx_t *)tr->ctx : NULL;

	if (!ctx)
		return;
	ctx->mgmt_rx_count = 0;
	ctx->data_rx_count = 0;
	ctx->eapol_rx_pending = 0;
	mock_sae_reset(ctx);
	ctx->twt_flow_next = 0;
}

size_t wifi_mgmt_transport_mock_ctx_size(void)
{
	return sizeof(wifi_mgmt_transport_mock_ctx_t);
}
