/*
 * 802.11ax FullMAC mock — in-process WiFi 6 NIC for #279 when RF is unavailable.
 */

#include "wifi_80211ax_mock.h"

#include "wifi_driver_packet.h"
#include "wifi_supplicant.h"

#include "net_loopback.h"
#include "net_wifi_mgmt.h"
#include "net_wire.h"
#include "net_checksum.h"
#include "net_udp.h"
#include "net_wifi_he.h"

#include "fl/mem_asm.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MOCK_RX_SLOTS 4u
#define MOCK_RX_MAX   FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
	size_t len;
	uint8_t data[MOCK_RX_MAX];
} mock_rx_slot_t;

typedef struct {
	wifi_fullmac_t dev;
	fl_net_driver_t netdev;
	mock_rx_slot_t rx[MOCK_RX_SLOTS];
	unsigned rx_head;
	unsigned rx_count;
	int up;
	uint8_t sta_mac[6];
	uint8_t ap_bssid[6];
	char joined_ssid[FL_WIFI_SSID_MAX + 1u];
	wifi_auth_mode_t joined_auth;
	uint8_t joined_he;
	uint8_t joined_6ghz;
	uint8_t joined_bss_color;
	wifi_network_t scan[3];
	uint16_t scan_count;
} wifi_80211ax_mock_ctx_t;

static wifi_80211ax_mock_ctx_t s_mock;

static const uint8_t s_ax_he_probe_ies[] = {
	FL_WIFI_ELEM_ID_EXTENSION, 18u, FL_WIFI_EXT_HE_CAPABILITIES,
	0x00, 0x80, 0x00, 0x06, 0x00, 0x00,
	0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	FL_WIFI_ELEM_ID_EXTENSION, 5u, FL_WIFI_EXT_HE_OPERATION, 0x02, 0x00, 0x00, 0x05
};

static void mock_seed_scan(wifi_80211ax_mock_ctx_t *ctx)
{
	memset(ctx->scan, 0, sizeof(ctx->scan));
	ctx->scan_count = 3;

	strncpy(ctx->scan[0].ssid, "MockAx6", sizeof(ctx->scan[0].ssid) - 1u);
	ctx->scan[0].bssid[0] = 0x02;
	ctx->scan[0].bssid[1] = 0xaa;
	ctx->scan[0].bssid[5] = 0x01;
	ctx->scan[0].rssi = -40;
	ctx->scan[0].channel = 37;
	ctx->scan[0].freq = 6135;
	ctx->scan[0].auth_mode = WIFI_AUTH_WPA3_SAE;

	strncpy(ctx->scan[1].ssid, "LegacyAC", sizeof(ctx->scan[1].ssid) - 1u);
	ctx->scan[1].bssid[0] = 0x02;
	ctx->scan[1].bssid[1] = 0xac;
	ctx->scan[1].bssid[5] = 0x02;
	ctx->scan[1].rssi = -55;
	ctx->scan[1].channel = 36;
	ctx->scan[1].freq = 5180;
	ctx->scan[1].auth_mode = WIFI_AUTH_WPA2_PSK;

	strncpy(ctx->scan[2].ssid, "MockOpen", sizeof(ctx->scan[2].ssid) - 1u);
	ctx->scan[2].bssid[0] = 0x02;
	ctx->scan[2].bssid[1] = 0x00;
	ctx->scan[2].bssid[5] = 0x03;
	ctx->scan[2].rssi = -60;
	ctx->scan[2].channel = 6;
	ctx->scan[2].freq = 2437;
	ctx->scan[2].auth_mode = WIFI_AUTH_OPEN;
}

static int mock_rx_enqueue(wifi_80211ax_mock_ctx_t *ctx, const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!ctx || !frame || len == 0 || len > MOCK_RX_MAX)
		return -1;
	if (ctx->rx_count >= MOCK_RX_SLOTS)
		return -1;
	idx = (ctx->rx_head + ctx->rx_count) % MOCK_RX_SLOTS;
	memcpy(ctx->rx[idx].data, frame, len);
	ctx->rx[idx].len = len;
	ctx->rx_count++;
	return 0;
}

static fl_result_t mock_build_ip_reply(const uint8_t *req_ip, size_t req_ip_len,
                                         const void *payload, size_t payload_len,
                                         uint8_t *out_ip, size_t out_cap)
{
	size_t hdr_len;
	uint16_t csum;

	if (!req_ip || req_ip_len < FL_NET_IPV4_HDR_LEN_MIN || !out_ip)
		return FL_RESULT_ERR;
	hdr_len = (size_t)((req_ip[0] & 0x0fu) * 4u);
	if (hdr_len + payload_len > out_cap)
		return FL_RESULT_ERR;
	memcpy(out_ip, req_ip, hdr_len);
	if (payload_len > 0u && payload)
		memcpy(out_ip + hdr_len, payload, payload_len);
	{
		uint8_t tmp[4];
		memcpy(tmp, out_ip + 12, 4);
		memcpy(out_ip + 12, out_ip + 16, 4);
		memcpy(out_ip + 16, tmp, 4);
	}
	{
		uint16_t total = (uint16_t)(hdr_len + payload_len);
		out_ip[2] = (uint8_t)(total >> 8);
		out_ip[3] = (uint8_t)(total & 0xff);
	}
	out_ip[10] = 0;
	out_ip[11] = 0;
	csum = fl_net_checksum16(out_ip, hdr_len);
	out_ip[10] = (uint8_t)(csum >> 8);
	out_ip[11] = (uint8_t)(csum & 0xff);
	return FL_RESULT_OK;
}

static fl_result_t mock_netdev_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)((char *)drv -
								   offsetof(wifi_80211ax_mock_ctx_t,
									    netdev));
	size_t ip_off;
	size_t ip_len;
	uint8_t eth_reply[MOCK_RX_MAX];
	size_t eth_len = 0;

	if (!ctx->up || !frame)
		return FL_RESULT_INVAL;
	if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (wifi_driver_packet_validate_tx(frame->data, frame->len) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (!fl_net_wire_parse_eth_ipv4(frame->data, frame->len, &ip_off, &ip_len, NULL))
		return FL_RESULT_OK;

	{
		const uint8_t *ip = frame->data + ip_off;
		uint8_t ip_reply[MOCK_RX_MAX];
		size_t ip_reply_len = 0;

		if (ip_len >= FL_NET_IPV4_HDR_LEN_MIN && ip[9] == FL_NET_IP_PROTO_UDP) {
			size_t ihl = (size_t)((ip[0] & 0x0fu) * 4u);
			const uint8_t *udp = ip + ihl;
			uint16_t sport = (uint16_t)(((uint16_t)udp[0] << 8) | (uint16_t)udp[1]);
			uint16_t dport = (uint16_t)(((uint16_t)udp[2] << 8) | (uint16_t)udp[3]);
			uint16_t udp_total = (uint16_t)(((uint16_t)udp[4] << 8) | (uint16_t)udp[5]);
			size_t payload_len = (size_t)udp_total - FL_NET_UDP_HDR_LEN;
			uint32_t src_be = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) |
					  ((uint32_t)ip[14] << 16) | ((uint32_t)ip[15] << 24);
			uint32_t dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) |
					  ((uint32_t)ip[18] << 16) | ((uint32_t)ip[19] << 24);
			uint8_t udp_buf[FL_NET_UDP_HDR_LEN + 576];
			size_t udp_len;

			if (ihl + udp_total <= ip_len && payload_len > 0u) {
				udp_len = fl_net_udp_build_datagram(
					udp_buf, sizeof(udp_buf), dst_be, src_be, dport, sport,
					udp + FL_NET_UDP_HDR_LEN, payload_len);
				if (udp_len > 0u &&
				    mock_build_ip_reply(ip, ip_len, udp_buf, udp_len, ip_reply,
							sizeof(ip_reply)) == FL_RESULT_OK)
					ip_reply_len =
						(size_t)(((ip_reply[2] & 0xffu) << 8) |
							 (ip_reply[3] & 0xffu));
			}
		}
		if (ip_reply_len > 0u) {
			eth_len = fl_net_wire_build_eth_ipv4(eth_reply, sizeof(eth_reply),
							     ctx->sta_mac, ctx->ap_bssid,
							     ip_reply, ip_reply_len);
			if (eth_len > 0u)
				(void)mock_rx_enqueue(ctx, eth_reply, eth_len);
		}
	}
	return FL_RESULT_OK;
}

static fl_result_t mock_netdev_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)((char *)drv -
								   offsetof(wifi_80211ax_mock_ctx_t,
									    netdev));
	fl_result_t rc;

	if (!out)
		return FL_RESULT_INVAL;
	if (ctx->rx_count == 0)
		return FL_RESULT_TIMEDOUT;
	if (out->cap < ctx->rx[ctx->rx_head].len)
		return FL_RESULT_ERR;
	out->len = ctx->rx[ctx->rx_head].len;
	memcpy(out->data, ctx->rx[ctx->rx_head].data, out->len);
	ctx->rx_head = (ctx->rx_head + 1u) % MOCK_RX_SLOTS;
	ctx->rx_count--;
	rc = FL_RESULT_OK;
	(void)fl_net_wire_check_rx_fill(out, out->len);
	return rc;
}

static int mock_init(wifi_fullmac_t *dev)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)dev;

	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_FW_READY;
	mock_seed_scan(ctx);
	return 0;
}

static int mock_deinit(wifi_fullmac_t *dev)
{
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_DOWN;
	return 0;
}

static int mock_reset(wifi_fullmac_t *dev)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)dev;

	if (!dev)
		return -1;
	ctx->up = 0;
	ctx->rx_head = 0;
	ctx->rx_count = 0;
	ctx->joined_ssid[0] = '\0';
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static int mock_start_scan(wifi_fullmac_t *dev, const char *ssid)
{
	(void)ssid;
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_SCANNING;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static int mock_get_scan_results(wifi_fullmac_t *dev, wifi_network_t *networks,
				 uint16_t *count)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)dev;
	uint16_t n;
	uint16_t i;

	if (!dev || !networks || !count)
		return -1;
	n = ctx->scan_count;
	if (n > *count)
		n = *count;
	for (i = 0; i < n; i++)
		networks[i] = ctx->scan[i];
	*count = n;
	return 0;
}

static int mock_get_he_capabilities(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap)
{
	if (!dev || !he_cap)
		return -1;
	memset(he_cap, 0, sizeof(*he_cap));
	he_cap->ofdma_dl_supported = true;
	he_cap->ofdma_ul_supported = true;
	he_cap->mcs_nss[0] = 0x44;
	he_cap->mcs_nss[1] = 0x44;
	he_cap->max_ampdu_len_exp = 3;
	return 0;
}

static int mock_setup_twt(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt)
{
	(void)dev;
	(void)twt;
	return 0;
}

static int mock_teardown_twt(wifi_fullmac_t *dev, uint8_t flow_id)
{
	(void)dev;
	(void)flow_id;
	return 0;
}

static int mock_deauthenticate(wifi_fullmac_t *dev, uint16_t reason)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)dev;

	(void)reason;
	if (!dev)
		return -1;
	ctx->up = 0;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static const wifi_fullmac_ops_t s_mock_ops = {
	.init = mock_init,
	.deinit = mock_deinit,
	.reset = mock_reset,
	.start_scan = mock_start_scan,
	.get_scan_results = mock_get_scan_results,
	.get_he_capabilities = mock_get_he_capabilities,
	.setup_twt = mock_setup_twt,
	.teardown_twt = mock_teardown_twt,
	.deauthenticate = mock_deauthenticate,
};

static const wifi_network_t *mock_find_ssid(wifi_80211ax_mock_ctx_t *ctx, const char *ssid)
{
	uint16_t i;

	if (!ctx || !ssid)
		return NULL;
	for (i = 0; i < ctx->scan_count; i++) {
		if (!strcmp(ctx->scan[i].ssid, ssid))
			return &ctx->scan[i];
	}
	return NULL;
}

static int mock_run_supplicant_ota(wifi_80211ax_mock_ctx_t *ctx,
				    const fl_net_wifi_cred_t *cred,
				    const wifi_network_t *ap)
{
	wifi_supplicant_t supp;
	uint8_t auth_frame[64];
	uint8_t assoc_frame[200];
	uint8_t msg1[64];
	uint8_t msg3[64];
	size_t frame_len = 0;
	uint8_t auth_mode = FL_WIFI_AUTH_OPEN;

	if (!ctx || !cred || !ap)
		return -1;

	if (wifi_supplicant_init(&supp, ap->bssid) != 0)
		return -1;
	if (wifi_supplicant_set_credentials(&supp, cred->ssid, cred->passphrase) != 0 ||
	    wifi_supplicant_set_sta_addr(&supp, ctx->sta_mac) != 0) {
		(void)wifi_supplicant_deinit(&supp);
		return -1;
	}

	if (fl_net_wifi_mgmt_build_auth_req(ctx->sta_mac, ap->bssid, auth_frame,
					    sizeof(auth_frame), &frame_len) != FL_RESULT_OK) {
		(void)wifi_supplicant_deinit(&supp);
		return -1;
	}

	switch (ap->auth_mode) {
	case WIFI_AUTH_WPA3_SAE:
		auth_mode = FL_WIFI_AUTH_WPA3_SAE;
		if (wifi_supplicant_start_sae_handshake(&supp) != 0 ||
		    wifi_supplicant_process_sae_commit(&supp, (const uint8_t *)"commit", 6) !=
			    0 ||
		    wifi_supplicant_process_sae_confirm(&supp, (const uint8_t *)"confirm",
							7) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
		break;
	case WIFI_AUTH_WPA2_PSK:
		auth_mode = FL_WIFI_AUTH_WPA2_PSK;
		if (wifi_supplicant_derive_pmk_psk(&supp, cred->ssid, cred->passphrase) != 0 ||
		    wifi_supplicant_start_4way_handshake(&supp) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
		memset(msg1, 0, sizeof(msg1));
		msg1[0] = 0x01;
		if (wifi_supplicant_process_msg1(&supp, msg1, 40) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
		memset(msg3, 0, sizeof(msg3));
		msg3[0] = 0x03;
		if (wifi_supplicant_process_msg3(&supp, msg3, 38) != 0) {
			(void)wifi_supplicant_deinit(&supp);
			return -1;
		}
		break;
	default:
		auth_mode = FL_WIFI_AUTH_OPEN;
		break;
	}

	if (fl_net_wifi_mgmt_build_assoc_req(cred->ssid, ap->bssid, ctx->sta_mac, auth_mode,
					     assoc_frame, sizeof(assoc_frame),
					     &frame_len) != FL_RESULT_OK) {
		(void)wifi_supplicant_deinit(&supp);
		return -1;
	}

	if (ap->auth_mode != WIFI_AUTH_OPEN &&
	    wifi_supplicant_get_state(&supp) != WIFI_SUPP_STATE_AUTHENTICATED) {
		(void)wifi_supplicant_deinit(&supp);
		return -1;
	}

	(void)wifi_supplicant_deinit(&supp);
	return 0;
}

int wifi_80211ax_mock_attach(wifi_fullmac_t **out_dev)
{
	wifi_80211ax_mock_ctx_t *ctx = &s_mock;

	if (!out_dev)
		return -1;

	memset(ctx, 0, sizeof(*ctx));
	strncpy(ctx->dev.name, "mock_ax0", sizeof(ctx->dev.name) - 1u);
	ctx->dev.bus_type = WIFI_FULLMAC_BUS_PCIE;
	ctx->dev.state = WIFI_FULLMAC_STATE_DOWN;
	ctx->dev.vendor_id = 0x8086u;
	ctx->dev.device_id = 0x2725u;
	ctx->dev.ops = &s_mock_ops;
	ctx->dev.netdev = &ctx->netdev;
	ctx->netdev.send = mock_netdev_send;
	ctx->netdev.recv = mock_netdev_recv;
	ctx->netdev.mtu = FL_NET_ETH_MTU_DEFAULT;
	fl_net_loopback_mac_host(ctx->sta_mac);
	mock_seed_scan(ctx);
	if (mock_init(&ctx->dev) != 0)
		return -1;
	*out_dev = &ctx->dev;
	return 0;
}

void wifi_80211ax_mock_detach(wifi_fullmac_t *dev)
{
	if (!dev)
		return;
	(void)mock_deinit(dev);
	asm_mem_zero(&s_mock, sizeof(s_mock));
}

int wifi_80211ax_mock_connect(wifi_fullmac_t *dev, const fl_net_wifi_cred_t *cred)
{
	wifi_80211ax_mock_ctx_t *ctx = (wifi_80211ax_mock_ctx_t *)dev;
	const wifi_network_t *ap;

	if (!dev || !cred || !cred->ssid[0])
		return -1;

	ap = mock_find_ssid(ctx, cred->ssid);
	if (!ap)
		return -1;

	if (ap->auth_mode != WIFI_AUTH_OPEN && cred->passphrase[0] == '\0')
		return -1;

	memcpy(ctx->ap_bssid, ap->bssid, 6);
	strncpy(ctx->joined_ssid, cred->ssid, sizeof(ctx->joined_ssid) - 1u);
	ctx->joined_auth = ap->auth_mode;
	ctx->joined_he = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_6ghz = (ap->auth_mode == WIFI_AUTH_WPA3_SAE) ? 1u : 0u;
	ctx->joined_bss_color = ctx->joined_he ? 5u : 0u;

	if (mock_run_supplicant_ota(ctx, cred, ap) != 0)
		return -1;

	ctx->up = 1;
	ctx->rx_head = 0;
	ctx->rx_count = 0;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}

void wifi_80211ax_mock_enrich_scan_entry(size_t index, fl_net_wifi_scan_entry_t *entry)
{
	if (!entry)
		return;
	if (index == 0u) {
		(void)fl_net_wifi_scan_enrich_from_ies(s_ax_he_probe_ies,
						       sizeof(s_ax_he_probe_ies), entry);
		entry->band = FL_WIFI_BAND_6GHZ;
		entry->channel = 37;
		entry->channel_width_mhz = 160;
		entry->auth_mode = FL_WIFI_AUTH_WPA3_SAE;
	} else if (index == 1u) {
		entry->he_supported = 0;
		entry->band = FL_WIFI_BAND_5GHZ;
		entry->channel = 36;
		entry->channel_width_mhz = 80;
		entry->auth_mode = FL_WIFI_AUTH_WPA2_PSK;
	} else if (index == 2u) {
		entry->he_supported = 0;
		entry->band = FL_WIFI_BAND_2GHZ;
		entry->channel = 6;
		entry->channel_width_mhz = 20;
		entry->auth_mode = FL_WIFI_AUTH_OPEN;
	}
}

void wifi_80211ax_mock_fill_he_cap(fl_net_wifi_he_cap_t *cap)
{
	if (!cap)
		return;
	memset(cap, 0, sizeof(*cap));
	if (!s_mock.up)
		return;
	cap->max_nss_rx = 4;
	cap->max_nss_tx = 4;
	cap->supports_ofdma = s_mock.joined_he;
	cap->supports_mu_mimo = s_mock.joined_he;
	cap->supports_twt = s_mock.joined_he;
	cap->supports_6ghz = s_mock.joined_6ghz;
	cap->bss_color = s_mock.joined_bss_color;
	cap->channel_width_mhz = s_mock.joined_he ? 160u : 80u;
}
