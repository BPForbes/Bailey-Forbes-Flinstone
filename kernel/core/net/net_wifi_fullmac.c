/*
 * FullMAC Wi-Fi NIC shim (#328 Task 1.2).
 *
 * Lab: packet-validated netdev ring (no kernel L2 socket).
 * Physical: nl80211 control (driver) + AF_PACKET data (driver); orchestration only.
 */
#include "net_wifi_fullmac.h"

#include "wifi_fullmac_afpacket.h"
#include "wifi_nl80211.h"
#include "net_wifi_he.h"
#include "net_wifi_mgmt_ota.h"
#include "net_wire.h"
#include "net_netdev.h"
#include "wifi_driver_packet.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FULLMAC_RX_SLOTS 8u
#define FULLMAC_RX_MAX FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
	size_t len;
	uint8_t data[FULLMAC_RX_MAX];
} fullmac_rx_slot_t;

typedef struct {
	fl_net_wifi_nl80211_t *nl;
	fl_net_driver_t driver;
	fl_net_wifi_fullmac_mode_t mode;
#if defined(__linux__)
	int pkt_fd;
#endif
	char ifname[FL_NET_WIFI_IFNAME_MAX];
	uint8_t sta_mac[6];
	fl_net_wifi_he_cap_t phy_he;
	fl_net_wifi_he_cap_t neg_he;
	uint8_t bands;
	int neg_he_valid;
	int up;
	fullmac_rx_slot_t rx[FULLMAC_RX_SLOTS];
	unsigned rx_head;
	unsigned rx_count;
} fl_net_wifi_fullmac_ctx_t;

static fl_net_wifi_fullmac_ctx_t g_fullmac;

static int fullmac_lab_enabled(void)
{
	const char *env = getenv("FL_NET_WIFI_FULLMAC_LAB");

	if (env && env[0] && strcmp(env, "0") != 0)
		return 1;
	return 0;
}

static const char *fullmac_resolve_ifname(const char *ifname)
{
	if (ifname && ifname[0])
		return ifname;
	{
		const char *env = getenv("FL_NET_WIFI_IFACE");

		if (env && env[0])
			return env;
	}
	return "wlan0";
}

static fl_result_t fullmac_rx_enqueue(const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!frame || len == 0 || len > FULLMAC_RX_MAX)
		return FL_RESULT_INVAL;
	if (g_fullmac.rx_count >= FULLMAC_RX_SLOTS)
		return FL_RESULT_ERR;
	idx = (g_fullmac.rx_head + g_fullmac.rx_count) % FULLMAC_RX_SLOTS;
	memcpy(g_fullmac.rx[idx].data, frame, len);
	g_fullmac.rx[idx].len = len;
	g_fullmac.rx_count++;
	return FL_RESULT_OK;
}

static fl_result_t fullmac_rx_dequeue(fl_net_frame_mut_t *out)
{
	if (!out)
		return FL_RESULT_INVAL;
	if (g_fullmac.rx_count == 0)
		return FL_RESULT_TIMEDOUT;
	if (out->cap < g_fullmac.rx[g_fullmac.rx_head].len)
		return FL_RESULT_ERR;
	out->len = g_fullmac.rx[g_fullmac.rx_head].len;
	memcpy(out->data, g_fullmac.rx[g_fullmac.rx_head].data, out->len);
	g_fullmac.rx_head = (g_fullmac.rx_head + 1u) % FULLMAC_RX_SLOTS;
	g_fullmac.rx_count--;
	return FL_RESULT_OK;
}

static fl_result_t fullmac_lab_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	(void)drv;
	if (!g_fullmac.up || !frame)
		return FL_RESULT_INVAL;
	if (fl_net_wire_check_tx(frame, g_fullmac.driver.mtu) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	return wifi_driver_packet_validate_tx(frame->data, frame->len);
}

static fl_result_t fullmac_lab_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	fl_result_t rc;

	(void)drv;
	if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	rc = fullmac_rx_dequeue(out);
	if (rc == FL_RESULT_OK)
		(void)fl_net_wire_check_rx_fill(out, out->len);
	return rc;
}

#if defined(__linux__)

static fl_result_t fullmac_physical_driver_send(fl_net_driver_t *drv,
						const fl_net_frame_view_t *frame)
{
	uint32_t ifindex = 0;
	fl_result_t rc;

	(void)drv;
	if (!g_fullmac.up || !frame || !frame->data || frame->len < 14u)
		return FL_RESULT_INVAL;
	if (fl_net_wire_check_tx(frame, g_fullmac.driver.mtu) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	if (wifi_driver_packet_validate_tx(frame->data, frame->len) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (!g_fullmac.nl || g_fullmac.pkt_fd < 0)
		return FL_RESULT_ERR;
	if (fl_net_wifi_nl80211_ifindex(g_fullmac.nl, &ifindex) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	rc = wifi_fullmac_afpacket_send(g_fullmac.pkt_fd, ifindex, frame->data, frame->len);
	return rc;
}

static fl_result_t fullmac_physical_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	size_t n = 0;
	fl_result_t rc;

	(void)drv;
	if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	if (g_fullmac.pkt_fd < 0)
		return FL_RESULT_ERR;
	rc = wifi_fullmac_afpacket_recv(g_fullmac.pkt_fd, out->data, out->cap, &n);
	if (rc != FL_RESULT_OK)
		return rc;
	if (fl_net_wire_check_rx_fill(out, n) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	(void)wifi_driver_packet_ingest_rx(out->data, out->len, NULL);
	return FL_RESULT_OK;
}

#endif /* __linux__ */

static void fullmac_setup_driver(fl_net_wifi_fullmac_mode_t mode)
{
	memset(&g_fullmac.driver, 0, sizeof(g_fullmac.driver));
	g_fullmac.driver.mtu = FL_NET_ETH_MTU_DEFAULT;
	g_fullmac.driver.impl = &g_fullmac;
	g_fullmac.mode = mode;
	g_fullmac.up = 1;
	if (mode == FL_NET_WIFI_FULLMAC_MODE_LAB) {
		g_fullmac.driver.send = fullmac_lab_driver_send;
		g_fullmac.driver.recv = fullmac_lab_driver_recv;
		return;
	}
#if defined(__linux__)
	g_fullmac.driver.send = fullmac_physical_driver_send;
	g_fullmac.driver.recv = fullmac_physical_driver_recv;
#endif
}

int fl_net_wifi_fullmac_available(void)
{
	if (fullmac_lab_enabled())
		return 1;
	return fl_net_wifi_nl80211_available();
}

fl_result_t fl_net_wifi_fullmac_init(const char *ifname)
{
	const char *iface;
	fl_result_t r;

	if (g_fullmac.up)
		return FL_RESULT_OK;

	memset(&g_fullmac, 0, sizeof(g_fullmac));
#if defined(__linux__)
	g_fullmac.pkt_fd = -1;
#endif

	if (fullmac_lab_enabled()) {
		strncpy(g_fullmac.ifname, "wlan-lab", sizeof(g_fullmac.ifname) - 1u);
		g_fullmac.ifname[sizeof(g_fullmac.ifname) - 1u] = '\0';
		(void)ifname;
		fullmac_setup_driver(FL_NET_WIFI_FULLMAC_MODE_LAB);
		return FL_RESULT_OK;
	}

	if (!fl_net_wifi_nl80211_available())
		return FL_RESULT_NOSYS;

	iface = fullmac_resolve_ifname(ifname);
	strncpy(g_fullmac.ifname, iface, sizeof(g_fullmac.ifname) - 1u);
	g_fullmac.ifname[sizeof(g_fullmac.ifname) - 1u] = '\0';

	r = fl_net_wifi_nl80211_init(&g_fullmac.nl, g_fullmac.ifname);
	if (r != FL_RESULT_OK)
		return r;

	(void)fl_net_wifi_nl80211_get_wiphy_caps(g_fullmac.nl, &g_fullmac.phy_he,
						 &g_fullmac.bands);
	(void)fl_net_wifi_nl80211_sta_mac(g_fullmac.nl, g_fullmac.sta_mac);
	(void)fl_net_wifi_fullmac_mgmt_ota_attach(g_fullmac.nl, g_fullmac.sta_mac,
						  &g_fullmac.phy_he);

#if defined(__linux__)
	{
		uint32_t ifindex = 0;

		if (fl_net_wifi_nl80211_ifindex(g_fullmac.nl, &ifindex) != FL_RESULT_OK ||
		    wifi_fullmac_afpacket_open((unsigned int)ifindex, &g_fullmac.pkt_fd) !=
			    FL_RESULT_OK) {
			fl_net_wifi_fullmac_mgmt_ota_detach();
			fl_net_wifi_nl80211_deinit(g_fullmac.nl);
			g_fullmac.nl = NULL;
			return FL_RESULT_NOSYS;
		}
	}
	fullmac_setup_driver(FL_NET_WIFI_FULLMAC_MODE_PHYSICAL);
#else
	fl_net_wifi_nl80211_deinit(g_fullmac.nl);
	g_fullmac.nl = NULL;
	return FL_RESULT_NOSYS;
#endif

	return FL_RESULT_OK;
}

void fl_net_wifi_fullmac_deinit(void)
{
	fl_net_wifi_fullmac_mgmt_ota_detach();
#if defined(__linux__)
	wifi_fullmac_afpacket_close(&g_fullmac.pkt_fd);
#endif
	if (g_fullmac.nl) {
		fl_net_wifi_nl80211_deinit(g_fullmac.nl);
		g_fullmac.nl = NULL;
	}
	g_fullmac.neg_he_valid = 0;
	g_fullmac.up = 0;
	g_fullmac.rx_head = 0;
	g_fullmac.rx_count = 0;
	g_fullmac.mode = FL_NET_WIFI_FULLMAC_MODE_NONE;
	memset(&g_fullmac.driver, 0, sizeof(g_fullmac.driver));
}

int fl_net_wifi_fullmac_active(void)
{
	return g_fullmac.up;
}

fl_net_wifi_fullmac_mode_t fl_net_wifi_fullmac_mode(void)
{
	return g_fullmac.mode;
}

int fl_net_wifi_fullmac_is_lab(void)
{
	return g_fullmac.mode == FL_NET_WIFI_FULLMAC_MODE_LAB;
}

int fl_net_wifi_fullmac_is_physical(void)
{
	return g_fullmac.mode == FL_NET_WIFI_FULLMAC_MODE_PHYSICAL;
}

fl_net_driver_t *fl_net_wifi_fullmac_driver(void)
{
	return g_fullmac.up ? &g_fullmac.driver : NULL;
}

const char *fl_net_wifi_fullmac_ifname(void)
{
	return g_fullmac.up ? g_fullmac.ifname : NULL;
}

fl_net_wifi_nl80211_t *fl_net_wifi_fullmac_nl80211(void)
{
	return g_fullmac.nl;
}

fl_result_t fl_net_wifi_fullmac_lab_inject_rx(const uint8_t *frame, size_t len)
{
	if (!fl_net_wifi_fullmac_is_lab())
		return FL_RESULT_NOSYS;
	return fullmac_rx_enqueue(frame, len);
}

fl_result_t fl_net_wifi_fullmac_scan(uint8_t band, const char *ssid, unsigned timeout_ms)
{
	(void)band;
	if (!g_fullmac.up)
		return FL_RESULT_NOSYS;
	if (fl_net_wifi_fullmac_is_lab())
		return FL_RESULT_NOSYS;
	if (!g_fullmac.nl)
		return FL_RESULT_NOSYS;
	{
		fl_result_t r = fl_net_wifi_nl80211_trigger_scan(g_fullmac.nl, ssid);

		if (r != FL_RESULT_OK)
			return r;
	}
	(void)fl_net_wifi_nl80211_poll(g_fullmac.nl, timeout_ms ? timeout_ms : 3000u);
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_fullmac_scan_result(fl_net_wifi_scan_entry_t *entries, size_t cap,
					    size_t *count_out)
{
	fl_result_t r;
	size_t i;
	size_t count = 0;
	fl_net_wifi_scan_entry_t tmp[64];

	if (!entries || !count_out || cap == 0u)
		return FL_RESULT_INVAL;
	if (!g_fullmac.up || fl_net_wifi_fullmac_is_lab() || !g_fullmac.nl)
		return FL_RESULT_NOSYS;

	r = fl_net_wifi_nl80211_get_scan(g_fullmac.nl, tmp, cap < 64u ? cap : 64u, &count);
	if (r != FL_RESULT_OK && count == 0u)
		return r;

	*count_out = 0;
	for (i = 0; i < count && *count_out < cap; i++) {
		entries[*count_out] = tmp[i];
		(*count_out)++;
	}
	if (fl_net_wifi_fullmac_mgmt_ota())
		fl_net_wifi_mgmt_ota_merge_scan(fl_net_wifi_fullmac_mgmt_ota(), entries, *count_out);
	return *count_out > 0u ? FL_RESULT_OK : FL_RESULT_ERR;
}

fl_result_t fl_net_wifi_fullmac_disconnect(void)
{
	if (!g_fullmac.up)
		return FL_RESULT_NOSYS;
	g_fullmac.neg_he_valid = 0;
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_fullmac_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
	if (!cap_out)
		return FL_RESULT_INVAL;
	if (!g_fullmac.up)
		return FL_RESULT_NOSYS;
	if (g_fullmac.neg_he_valid) {
		*cap_out = g_fullmac.neg_he;
		return FL_RESULT_OK;
	}
	if (fl_net_wifi_fullmac_is_lab()) {
		memset(cap_out, 0, sizeof(*cap_out));
		cap_out->max_nss_rx = 2u;
		cap_out->max_nss_tx = 2u;
		cap_out->channel_width_mhz = 80u;
		cap_out->supports_ofdma = 1u;
		cap_out->supports_twt = 1u;
		return FL_RESULT_OK;
	}
	*cap_out = g_fullmac.phy_he;
	return FL_RESULT_OK;
}

void fl_net_wifi_fullmac_set_negotiated_he(const fl_net_wifi_he_cap_t *cap)
{
	if (!cap) {
		g_fullmac.neg_he_valid = 0;
		return;
	}
	g_fullmac.neg_he = *cap;
	g_fullmac.neg_he_valid = 1;
}

int fl_net_wifi_fullmac_mt7921_detected(void)
{
	if (!g_fullmac.nl)
		return 0;
	return fl_net_wifi_nl80211_mt7921_detected(g_fullmac.nl);
}
