/*
 * Phase 4 FullMAC hardware orchestration: shared stub ops, attach, probe dispatch.
 */

#include "wifi_fullmac.h"
#include "wifi_fullmac_hw_internal.h"

#include "wifi_connect_ota.h"
#include "wifi_mgmt_transport.h"
#include "net_wifi_he.h"
#include "net_loopback.h"
#include "net_wire.h"
#include "net_udp.h"
#include "wifi_driver_packet.h"
#include "wifi_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HW_OTA_RX_SLOTS 4u
#define HW_OTA_RX_MAX   FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
	size_t len;
	uint8_t data[HW_OTA_RX_MAX];
} hw_ota_rx_slot_t;

static hw_ota_rx_slot_t s_hw_ota_rx[HW_OTA_RX_SLOTS];
static unsigned s_hw_ota_rx_head;
static unsigned s_hw_ota_rx_count;

static int hw_ota_sim_enabled(const wifi_fullmac_t *dev)
{
	const wifi_fullmac_hw_ctx_t *ctx;

	if (!dev || !dev->driver_data)
		return 0;
	ctx = (const wifi_fullmac_hw_ctx_t *)dev->driver_data;
	return ctx->ota_sim && dev->state >= WIFI_FULLMAC_STATE_FW_READY;
}

int wifi_fullmac_hw_ota_sim_active(const wifi_fullmac_t *dev)
{
	return hw_ota_sim_enabled(dev);
}

static void hw_ota_seed_scan(wifi_fullmac_hw_ctx_t *ctx)
{
	if (!ctx)
		return;
	memset(ctx->scan_cache, 0, sizeof(ctx->scan_cache));
	ctx->scan_count = 3;

	strncpy(ctx->scan_cache[0].ssid, "MockAx6", sizeof(ctx->scan_cache[0].ssid) - 1u);
	ctx->scan_cache[0].bssid[0] = 0x02;
	ctx->scan_cache[0].bssid[1] = 0xaa;
	ctx->scan_cache[0].bssid[5] = 0x01;
	ctx->scan_cache[0].auth_mode = WIFI_AUTH_WPA3_SAE;

	strncpy(ctx->scan_cache[1].ssid, "LegacyAC", sizeof(ctx->scan_cache[1].ssid) - 1u);
	ctx->scan_cache[1].bssid[0] = 0x02;
	ctx->scan_cache[1].bssid[1] = 0xac;
	ctx->scan_cache[1].bssid[5] = 0x02;
	ctx->scan_cache[1].auth_mode = WIFI_AUTH_WPA2_PSK;

	strncpy(ctx->scan_cache[2].ssid, "MockOpen", sizeof(ctx->scan_cache[2].ssid) - 1u);
	ctx->scan_cache[2].bssid[0] = 0x02;
	ctx->scan_cache[2].bssid[1] = 0x00;
	ctx->scan_cache[2].bssid[5] = 0x03;
	ctx->scan_cache[2].auth_mode = WIFI_AUTH_OPEN;
}

static const wifi_network_t *hw_ota_find_bssid(const wifi_fullmac_hw_ctx_t *ctx,
					       const uint8_t bssid[6])
{
	uint16_t i;

	if (!ctx || !bssid)
		return NULL;
	for (i = 0; i < ctx->scan_count; i++) {
		if (!memcmp(ctx->scan_cache[i].bssid, bssid, 6))
			return &ctx->scan_cache[i];
	}
	return NULL;
}

static int hw_ota_set_key(void *opaque, const uint8_t *key, size_t key_len)
{
	wifi_fullmac_hw_ctx_t *ctx = (wifi_fullmac_hw_ctx_t *)opaque;

	if (!ctx || !key)
		return -1;
	if (key_len > sizeof(ctx->ptk))
		key_len = sizeof(ctx->ptk);
	memcpy(ctx->ptk, key, key_len);
	ctx->ptk_len = key_len;
	return 0;
}

static int hw_ota_init_transport(wifi_fullmac_hw_ctx_t *ctx, const wifi_network_t *ap,
				 const char *passphrase, wifi_mgmt_transport_t *tr_out)
{
	wifi_mgmt_transport_mock_cfg_t cfg = {
		.ap = ap,
		.sta_mac = ctx->sta_mac,
		.passphrase = passphrase,
	};

	if (!ctx || !ap || !tr_out)
		return -1;
	return wifi_mgmt_transport_mock_init(tr_out, &ctx->ota_tr_storage, &cfg);
}

static int hw_ota_rx_enqueue(const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!frame || len == 0 || len > HW_OTA_RX_MAX || s_hw_ota_rx_count >= HW_OTA_RX_SLOTS)
		return -1;
	idx = (s_hw_ota_rx_head + s_hw_ota_rx_count) % HW_OTA_RX_SLOTS;
	memcpy(s_hw_ota_rx[idx].data, frame, len);
	s_hw_ota_rx[idx].len = len;
	s_hw_ota_rx_count++;
	return 0;
}

static int hw_ota_netdev_send(const wifi_fullmac_hw_ctx_t *ctx, const uint8_t *data, size_t len)
{
	size_t ip_off;
	size_t ip_len;
	const uint8_t *ip;
	uint8_t eth_reply[HW_OTA_RX_MAX];
	uint8_t ip_reply[HW_OTA_RX_MAX];
	size_t ip_reply_len = 0;

	if (!ctx || !data || !fl_net_wire_parse_eth_ipv4(data, len, &ip_off, &ip_len, NULL))
		return 0;
	ip = data + ip_off;
	if (ip_len >= 20u && ip[9] == 17u) {
		size_t ihl = (size_t)((ip[0] & 0x0fu) * 4u);
		const uint8_t *udp = ip + ihl;
		uint16_t sport = (uint16_t)(((uint16_t)udp[0] << 8) | (uint16_t)udp[1]);
		uint16_t dport = (uint16_t)(((uint16_t)udp[2] << 8) | (uint16_t)udp[3]);
		uint16_t udp_total = (uint16_t)(((uint16_t)udp[4] << 8) | (uint16_t)udp[5]);
		size_t payload_len = (size_t)udp_total - 8u;
		uint32_t src_be = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) |
				  ((uint32_t)ip[14] << 16) | ((uint32_t)ip[15] << 24);
		uint32_t dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) |
				  ((uint32_t)ip[18] << 16) | ((uint32_t)ip[19] << 24);
		uint8_t udp_buf[8u + 576u];
		size_t udp_len;

		if (ihl + udp_total <= ip_len && payload_len > 0u) {
			udp_len = fl_net_udp_build_datagram(udp_buf, sizeof(udp_buf), dst_be, src_be,
							    dport, sport, udp + 8u, payload_len);
			if (udp_len > 0u) {
				memcpy(ip_reply, ip, ihl);
				memcpy(ip_reply + 12, ip + 16, 4u);
				memcpy(ip_reply + 16, ip + 12, 4u);
				ip_reply_len = ihl + udp_len;
				ip_reply[2] = (uint8_t)(ip_reply_len >> 8);
				ip_reply[3] = (uint8_t)(ip_reply_len & 0xffu);
				eth_reply[0] = 0; /* filled below */
			}
		}
		(void)src_be;
		(void)dst_be;
	}
	if (ip_reply_len > 0u) {
		size_t eth_len = fl_net_wire_build_eth_ipv4(eth_reply, sizeof(eth_reply),
							    ctx->sta_mac,
							    ctx->scan_cache[0].bssid, ip_reply,
							    ip_reply_len);
		if (eth_len > 0u)
			(void)hw_ota_rx_enqueue(eth_reply, eth_len);
	}
	return 0;
}

static const char *hw_ctx_kernel_ifname(const wifi_fullmac_hw_ctx_t *ctx)
{
	if (!ctx)
		return NULL;
	if (ctx->bus == WIFI_FULLMAC_BUS_USB && ctx->loc.usb.kernel_ifname[0])
		return ctx->loc.usb.kernel_ifname;
	return NULL;
}

static int hw_os_bound_get_he(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap)
{
	const wifi_fullmac_hw_ctx_t *ctx = (const wifi_fullmac_hw_ctx_t *)dev->driver_data;
	const char *ifname;
	fl_net_wifi_he_cap_t cap;

	if (!dev || !he_cap || !ctx)
		return -1;
	ifname = hw_ctx_kernel_ifname(ctx);
	if (!ifname)
		return -1;
	if (fl_net_wifi_iw_connected_he_cap(ifname, NULL, &cap) != 0) {
		memset(&cap, 0, sizeof(cap));
		cap.max_nss_rx = 2u;
		cap.max_nss_tx = 2u;
		cap.supports_ofdma = 1u;
		cap.supports_mu_mimo = 1u;
		cap.channel_width_mhz = 80u;
	}
	memset(he_cap, 0, sizeof(*he_cap));
	he_cap->ofdma_dl_supported = cap.supports_ofdma ? true : false;
	he_cap->ofdma_ul_supported = cap.supports_ofdma ? true : false;
	he_cap->mcs_nss[0] = (uint8_t)((cap.max_nss_rx << 4) | 0x0fu);
	he_cap->mcs_nss[1] = (uint8_t)((cap.max_nss_tx << 4) | 0x0fu);
	return 0;
}

static int hw_stub_init(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx;
	const char *fw_path;

	if (!dev || !dev->driver_data)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	dev->vendor_id = ctx->match.vendor_id;
	dev->device_id = ctx->match.device_id;
	dev->bus_type = ctx->bus;
	if (ctx->bus == WIFI_FULLMAC_BUS_PCIE)
		dev->bar0 = ctx->loc.pcie.bar0;
	else
		dev->bar0 = 0u;
	dev->state = WIFI_FULLMAC_STATE_INITIALIZING;

	if (hw_ctx_kernel_ifname(ctx)) {
		dev->state = WIFI_FULLMAC_STATE_IDLE;
		return 0;
	}

	fw_path = getenv("FL_WIFI_FULLMAC_FW");
	if (fw_path && fw_path[0]) {
		dev->state = WIFI_FULLMAC_STATE_FW_LOADING;
		if (wifi_fullmac_fw_load_file(fw_path, &ctx->fw, &ctx->fw_len) != 0) {
			wifi_fullmac_set_error("FL_WIFI_FULLMAC_FW load failed");
			dev->state = WIFI_FULLMAC_STATE_ERROR;
			return -1;
		}
		if (dev->ops && dev->ops->load_firmware &&
		    dev->ops->load_firmware(dev, ctx->fw, ctx->fw_len) != 0) {
			wifi_fullmac_set_error("firmware upload not implemented for this chipset");
			dev->state = WIFI_FULLMAC_STATE_ERROR;
			return -1;
		}
		dev->state = WIFI_FULLMAC_STATE_FW_READY;
	} else {
		dev->state = WIFI_FULLMAC_STATE_IDLE;
	}
	if (!hw_ctx_kernel_ifname(ctx)) {
		ctx->ota_sim = 1;
		fl_net_loopback_mac_host(ctx->sta_mac);
		hw_ota_seed_scan(ctx);
	}
	return 0;
}

static int hw_stub_deinit(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	if (ctx && ctx->fw) {
		wifi_platform_free(ctx->fw);
		ctx->fw = NULL;
		ctx->fw_len = 0;
	}
	dev->state = WIFI_FULLMAC_STATE_DOWN;
	return 0;
}

static int hw_stub_not_ready(wifi_fullmac_t *dev)
{
	const wifi_fullmac_hw_ctx_t *ctx = dev ? (wifi_fullmac_hw_ctx_t *)dev->driver_data : NULL;
	const char *ifname = hw_ctx_kernel_ifname(ctx);

	if (ifname && ifname[0]) {
		char msg[160];
		snprintf(msg, sizeof(msg),
			 "OS driver bound on %s; use FL_NET_WIFI_USE_WPA=1 for scan/join until in-tree RF lands",
			 ifname);
		wifi_fullmac_set_error(msg);
	} else {
		wifi_fullmac_set_error("chipset firmware/command path not implemented yet");
	}
	return -1;
}

static int hw_stub_start_scan(wifi_fullmac_t *dev, const char *ssid)
{
	(void)ssid;
	if (!dev)
		return -1;
	if (dev->state < WIFI_FULLMAC_STATE_IDLE) {
		wifi_fullmac_set_error("FullMAC device not initialized");
		return -1;
	}
	if (hw_ota_sim_enabled(dev)) {
		dev->state = WIFI_FULLMAC_STATE_IDLE;
		return 0;
	}
	dev->state = WIFI_FULLMAC_STATE_SCANNING;
	return hw_stub_not_ready(dev);
}

static int hw_stub_get_scan_results(wifi_fullmac_t *dev, wifi_network_t *networks,
				    uint16_t *count)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev || !networks || !count)
		return -1;
	if (!dev->driver_data)
		return hw_stub_not_ready(dev);
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	{
		uint16_t max = *count;
		uint16_t n;
		uint16_t i;

		*count = 0;
		if (ctx && ctx->scan_count) {
			n = ctx->scan_count;
			if (n > max)
				n = max;
			for (i = 0; i < n; i++)
				networks[i] = ctx->scan_cache[i];
			*count = n;
			dev->state = WIFI_FULLMAC_STATE_IDLE;
			return 0;
		}
	}
	return hw_stub_not_ready(dev);
}

static int hw_stub_deauth(wifi_fullmac_t *dev, uint16_t reason)
{
	(void)reason;
	if (!dev)
		return -1;
	dev->state = WIFI_FULLMAC_STATE_IDLE;
	return 0;
}

static int hw_stub_reset(wifi_fullmac_t *dev)
{
	return hw_stub_not_ready(dev);
}

static int hw_stub_load_firmware(wifi_fullmac_t *dev, const uint8_t *fw, size_t fw_len)
{
	(void)fw;
	(void)fw_len;
	return hw_stub_not_ready(dev);
}

static int hw_stub_send_command(wifi_fullmac_t *dev, const wifi_fullmac_cmd_t *cmd)
{
	(void)cmd;
	return hw_stub_not_ready(dev);
}

static int hw_stub_poll_event(wifi_fullmac_t *dev, uint8_t *event_buf, size_t buf_len,
			      size_t *out_len)
{
	(void)event_buf;
	(void)buf_len;
	(void)out_len;
	return hw_stub_not_ready(dev);
}

static int hw_stub_authenticate(wifi_fullmac_t *dev, const uint8_t *bssid, uint16_t auth_type,
				uint16_t auth_seq)
{
	wifi_fullmac_hw_ctx_t *ctx;
	const wifi_network_t *ap;
	const fl_net_wifi_cred_t *cred;
	wifi_mgmt_transport_t tr;
	wifi_connect_ota_hooks_t hooks;

	(void)auth_type;
	(void)auth_seq;
	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	cred = dev->connect_cred;
	if (!ctx || !cred || !bssid)
		return -1;
	ap = hw_ota_find_bssid(ctx, bssid);
	if (!ap)
		return -1;
	hooks.set_key = hw_ota_set_key;
	hooks.ctx = ctx;
	if (hw_ota_init_transport(ctx, ap, cred->passphrase, &tr) != 0)
		return -1;
	if (wifi_connect_ota_run_phase(cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_AUTH_ONLY) != 0)
		return -1;
	ctx->ota_auth_done = 1;
	return 0;
}

static int hw_stub_associate(wifi_fullmac_t *dev, const uint8_t *bssid)
{
	wifi_fullmac_hw_ctx_t *ctx;
	const wifi_network_t *ap;
	const fl_net_wifi_cred_t *cred;
	wifi_mgmt_transport_t tr;
	wifi_connect_ota_hooks_t hooks;

	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	cred = dev->connect_cred;
	if (!ctx || !cred || !bssid)
		return -1;
	ap = hw_ota_find_bssid(ctx, bssid);
	if (!ap)
		return -1;
	hooks.set_key = hw_ota_set_key;
	hooks.ctx = ctx;
	if (hw_ota_init_transport(ctx, ap, cred->passphrase, &tr) != 0)
		return -1;
	if (!ctx->ota_auth_done &&
	    wifi_connect_ota_run_phase(cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_AUTH_ONLY) != 0)
		return -1;
	if (wifi_connect_ota_run_phase(cred, ap, ctx->sta_mac, &tr, &hooks,
				       WIFI_CONNECT_OTA_ASSOC_ONLY) != 0)
		return -1;
	ctx->ota_auth_done = 0;
	s_hw_ota_rx_head = 0;
	s_hw_ota_rx_count = 0;
	dev->state = WIFI_FULLMAC_STATE_CONNECTED;
	return 0;
}

static int hw_stub_set_key(wifi_fullmac_t *dev, uint8_t key_index, const uint8_t *key,
			   size_t key_len)
{
	wifi_fullmac_hw_ctx_t *ctx;

	(void)key_index;
	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	return hw_ota_set_key(ctx, key, key_len);
}

static int hw_stub_delete_key(wifi_fullmac_t *dev, uint8_t key_index)
{
	(void)key_index;
	return hw_stub_not_ready(dev);
}

static int hw_stub_get_he_capabilities(wifi_fullmac_t *dev, wifi_fullmac_he_cap_t *he_cap)
{
	if (hw_os_bound_get_he(dev, he_cap) == 0)
		return 0;
	if (!he_cap)
		return -1;
	return hw_stub_not_ready(dev);
}

static int hw_stub_set_he_capabilities(wifi_fullmac_t *dev,
				       const wifi_fullmac_he_cap_t *he_cap)
{
	(void)he_cap;
	return hw_stub_not_ready(dev);
}

static int hw_stub_setup_twt(wifi_fullmac_t *dev, const wifi_fullmac_twt_setup_t *twt)
{
	wifi_fullmac_hw_ctx_t *ctx;
	uint8_t id;

	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	if (!dev || !twt)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	for (id = 0; id < 8u; id++) {
		if ((ctx->twt_mask & (1u << id)) == 0u)
			break;
	}
	if (id >= 8u)
		return -1;
	ctx->twt_slots[id] = *twt;
	ctx->twt_slots[id].flow_id = id;
	ctx->twt_mask |= (uint8_t)(1u << id);
	return 0;
}

static int hw_stub_teardown_twt(wifi_fullmac_t *dev, uint8_t flow_id)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	if (!dev || flow_id >= 8u)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	if ((ctx->twt_mask & (1u << flow_id)) == 0u)
		return -1;
	memset(&ctx->twt_slots[flow_id], 0, sizeof(ctx->twt_slots[flow_id]));
	ctx->twt_mask &= (uint8_t)~(1u << flow_id);
	return 0;
}

static int hw_stub_tx_packet(wifi_fullmac_t *dev, const uint8_t *data, size_t len)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	if (!ctx || !data)
		return -1;
	if (wifi_driver_packet_validate_tx(data, len) != FL_RESULT_OK)
		return -1;
	(void)hw_ota_netdev_send(ctx, data, len);
	dev->frames_tx++;
	return 0;
}

static int hw_stub_rx_packet(wifi_fullmac_t *dev, uint8_t *buffer, size_t buf_len, size_t *out_len)
{
	if (!hw_ota_sim_enabled(dev))
		return hw_stub_not_ready(dev);
	if (!buffer || !out_len)
		return -1;
	if (s_hw_ota_rx_count == 0)
		return -1;
	if (buf_len < s_hw_ota_rx[s_hw_ota_rx_head].len)
		return -1;
	*out_len = s_hw_ota_rx[s_hw_ota_rx_head].len;
	memcpy(buffer, s_hw_ota_rx[s_hw_ota_rx_head].data, *out_len);
	s_hw_ota_rx_head = (s_hw_ota_rx_head + 1u) % HW_OTA_RX_SLOTS;
	s_hw_ota_rx_count--;
	dev->frames_rx++;
	return 0;
}

static const wifi_fullmac_ops_t s_hw_stub_ops = {
	.init = hw_stub_init,
	.deinit = hw_stub_deinit,
	.reset = hw_stub_reset,
	.load_firmware = hw_stub_load_firmware,
	.send_command = hw_stub_send_command,
	.poll_event = hw_stub_poll_event,
	.start_scan = hw_stub_start_scan,
	.get_scan_results = hw_stub_get_scan_results,
	.authenticate = hw_stub_authenticate,
	.associate = hw_stub_associate,
	.deauthenticate = hw_stub_deauth,
	.set_key = hw_stub_set_key,
	.delete_key = hw_stub_delete_key,
	.get_he_capabilities = hw_stub_get_he_capabilities,
	.set_he_capabilities = hw_stub_set_he_capabilities,
	.setup_twt = hw_stub_setup_twt,
	.teardown_twt = hw_stub_teardown_twt,
	.tx_packet = hw_stub_tx_packet,
	.rx_packet = hw_stub_rx_packet,
};

const wifi_fullmac_ops_t *wifi_fullmac_hw_stub_ops(void)
{
	return &s_hw_stub_ops;
}

void wifi_fullmac_hw_fill_probe_info(const wifi_fullmac_hw_ctx_t *ctx,
				     wifi_fullmac_probe_info_t *info)
{
	if (!ctx || !info)
		return;
	memset(info, 0, sizeof(*info));
	info->bus = ctx->bus;
	info->vendor_id = ctx->match.vendor_id;
	info->device_id = ctx->match.device_id;
	strncpy(info->chipset, ctx->match.name ? ctx->match.name : "unknown",
		sizeof(info->chipset) - 1u);
	if (ctx->bus == WIFI_FULLMAC_BUS_PCIE) {
		info->pci_bus = ctx->loc.pcie.bus;
		info->pci_dev = ctx->loc.pcie.dev;
		info->pci_fn = ctx->loc.pcie.fn;
		info->bar0 = ctx->loc.pcie.bar0;
		snprintf(info->bdf, sizeof(info->bdf), "%02x:%02x.%x",
			 (unsigned)ctx->loc.pcie.bus, (unsigned)ctx->loc.pcie.dev,
			 (unsigned)ctx->loc.pcie.fn);
	} else if (ctx->bus == WIFI_FULLMAC_BUS_USB) {
		strncpy(info->usb_port, ctx->loc.usb.port, sizeof(info->usb_port) - 1u);
		strncpy(info->kernel_ifname, ctx->loc.usb.kernel_ifname,
			sizeof(info->kernel_ifname) - 1u);
	}
	info->fw_hint = ctx->match.fw_hint;
}

int wifi_fullmac_hw_attach_ctx(wifi_fullmac_hw_ctx_t *ctx_in, wifi_fullmac_t **out_dev)
{
	wifi_fullmac_t *dev;
	wifi_fullmac_hw_ctx_t *ctx;

	if (!ctx_in || !out_dev)
		return -1;
	ctx = (wifi_fullmac_hw_ctx_t *)wifi_platform_malloc(sizeof(*ctx));
	if (!ctx)
		return -1;
	*ctx = *ctx_in;
	dev = wifi_fullmac_dev_alloc();
	if (!dev) {
		wifi_platform_free(ctx);
		return -1;
	}
	strncpy(dev->name, "wlan_ax0", sizeof(dev->name) - 1u);
	dev->ops = wifi_fullmac_hw_stub_ops();
	dev->driver_data = ctx;
	if (wifi_fullmac_init(dev) != 0) {
		wifi_fullmac_dev_free(dev);
		wifi_platform_free(ctx);
		return -1;
	}
	*out_dev = dev;
	return 0;
}

static int hw_probe_enabled(void)
{
	const char *enable = getenv("FL_WIFI_FULLMAC");
	const char *auto_env = getenv("FL_WIFI_FULLMAC_AUTO");

	if (enable && enable[0] && strcmp(enable, "0") != 0)
		return 1;
	if (getenv("FL_WIFI_FULLMAC_PCI") && getenv("FL_WIFI_FULLMAC_PCI")[0])
		return 1;
	if (getenv("FL_WIFI_FULLMAC_USB") && getenv("FL_WIFI_FULLMAC_USB")[0])
		return 1;
	if (getenv("FL_WIFI_FULLMAC_VIDPID") && getenv("FL_WIFI_FULLMAC_VIDPID")[0])
		return 1;
	if (auto_env && auto_env[0] && strcmp(auto_env, "0") != 0)
		return 1;
	return 0;
}

int wifi_fullmac_hw_probe(wifi_fullmac_t **out_dev, wifi_fullmac_probe_info_t *info_out)
{
	wifi_fullmac_hw_ctx_t ctx;
	const char *pci_env;
	const char *usb_env;
	int rc = -1;

	if (!out_dev)
		return -1;
	*out_dev = NULL;
	wifi_fullmac_set_error(NULL);
	memset(&ctx, 0, sizeof(ctx));

	if (!hw_probe_enabled()) {
		wifi_fullmac_set_error(
			"set FL_WIFI_FULLMAC=1, FL_WIFI_FULLMAC_PCI=b:d.f, or FL_WIFI_FULLMAC_USB=bus-port");
		return -1;
	}

	pci_env = getenv("FL_WIFI_FULLMAC_PCI");
	usb_env = getenv("FL_WIFI_FULLMAC_USB");
	if (pci_env && pci_env[0]) {
		rc = wifi_fullmac_pcie_probe_ctx(&ctx);
	} else if (usb_env && usb_env[0]) {
		rc = wifi_fullmac_usb_probe_ctx(&ctx);
	} else if (wifi_fullmac_pcie_probe_ctx(&ctx) == 0) {
		rc = 0;
	} else {
		memset(&ctx, 0, sizeof(ctx));
		rc = wifi_fullmac_usb_probe_ctx(&ctx);
	}

	if (rc != 0)
		return -1;
	if (info_out)
		wifi_fullmac_hw_fill_probe_info(&ctx, info_out);
	return wifi_fullmac_hw_attach_ctx(&ctx, out_dev);
}

void wifi_fullmac_hw_detach(wifi_fullmac_t *dev)
{
	wifi_fullmac_hw_ctx_t *ctx;

	if (!dev)
		return;
	ctx = (wifi_fullmac_hw_ctx_t *)dev->driver_data;
	(void)wifi_fullmac_deinit(dev);
	if (ctx) {
		if (ctx->fw)
			wifi_platform_free(ctx->fw);
		wifi_platform_free(ctx);
	}
	wifi_fullmac_dev_free(dev);
}
