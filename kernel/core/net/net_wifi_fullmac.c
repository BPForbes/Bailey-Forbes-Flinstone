/*
 * FullMAC Wi-Fi NIC driver shim — AF_PACKET data path + nl80211 control plane.
 * Copyright (c) Bailey-Forbes-Flinstone contributors.
 */
#include "net_wifi_fullmac.h"

#include "net_wifi_nl80211.h"
#include "net_wifi_he.h"
#include "net_wire.h"
#include "net_netdev.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

typedef struct {
	fl_net_wifi_nl80211_t *nl;
	fl_net_driver_t driver;
#if defined(__linux__)
	int pkt_fd;
#endif
	char ifname[16];
	fl_net_wifi_he_cap_t phy_he;
	fl_net_wifi_he_cap_t neg_he;
	uint8_t bands;
	int neg_he_valid;
	int connected;
} fl_net_wifi_fullmac_ctx_t;

static fl_net_wifi_fullmac_ctx_t g_fullmac;

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

#if defined(__linux__)

static fl_result_t fullmac_open_packet_socket(const char *ifname, unsigned int ifindex, int *fd_out)
{
	struct sockaddr_ll bind_addr;
	int fd;
	int flags;

	if (!ifname || !ifname[0] || !fd_out)
		return FL_RESULT_INVAL;
	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0)
		return FL_RESULT_NOSYS;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sll_family = AF_PACKET;
	bind_addr.sll_protocol = htons(ETH_P_ALL);
	bind_addr.sll_ifindex = (int)ifindex;
	if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
		close(fd);
		return FL_RESULT_NOSYS;
	}
	flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		(void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	*fd_out = fd;
	(void)ifname;
	return FL_RESULT_OK;
}

static fl_result_t fullmac_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	fl_net_wifi_fullmac_ctx_t *ctx;
	struct sockaddr_ll dest;
	ssize_t n;

	if (!drv || !frame || !frame->data || frame->len < 14u)
		return FL_RESULT_INVAL;
	if (fl_net_wire_check_tx(frame, drv->mtu) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	ctx = (fl_net_wifi_fullmac_ctx_t *)drv->impl;
	if (!ctx || ctx->pkt_fd < 0)
		return FL_RESULT_ERR;
	memset(&dest, 0, sizeof(dest));
	dest.sll_family = AF_PACKET;
	{
		uint32_t ifindex = 0;

		if (fl_net_wifi_nl80211_ifindex(ctx->nl, &ifindex) != FL_RESULT_OK)
			return FL_RESULT_ERR;
		dest.sll_ifindex = (int)ifindex;
	}
	dest.sll_halen = 6u;
	memcpy(dest.sll_addr, frame->data, 6u);
	n = sendto(ctx->pkt_fd, frame->data, frame->len, 0, (struct sockaddr *)&dest,
		   sizeof(dest));
	if (n < 0)
		return FL_RESULT_ERR;
	if ((size_t)n != frame->len)
		return FL_RESULT_ERR;
	return FL_RESULT_OK;
}

static fl_result_t fullmac_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	fl_net_wifi_fullmac_ctx_t *ctx;
	ssize_t n;

	if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
		return FL_RESULT_INVAL;
	if (fl_net_netdev_authz_check((unsigned)FL_AUTHZ_OP_NETDEV_IO) != FL_RESULT_OK)
		return FL_RESULT_ACCES;
	ctx = (fl_net_wifi_fullmac_ctx_t *)drv->impl;
	if (!ctx || ctx->pkt_fd < 0)
		return FL_RESULT_ERR;
	n = recv(ctx->pkt_fd, out->data, out->cap, MSG_DONTWAIT);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return FL_RESULT_TIMEDOUT;
		return FL_RESULT_ERR;
	}
	if (n < 14)
		return FL_RESULT_TIMEDOUT;
	if (fl_net_wire_check_rx_fill(out, (size_t)n) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	return FL_RESULT_OK;
}

#else /* !__linux__ */

static fl_result_t fullmac_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
	(void)drv;
	(void)frame;
	return FL_RESULT_NOSYS;
}

static fl_result_t fullmac_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
	(void)drv;
	(void)out;
	return FL_RESULT_NOSYS;
}

#endif

fl_result_t fl_net_wifi_fullmac_init(const char *ifname)
{
	const char *iface;
	fl_result_t r;
	uint32_t ifindex = 0;

	if (g_fullmac.nl)
		return FL_RESULT_OK;
	if (!fl_net_wifi_nl80211_available())
		return FL_RESULT_NOSYS;

	iface = fullmac_resolve_ifname(ifname);
	memset(&g_fullmac, 0, sizeof(g_fullmac));
	strncpy(g_fullmac.ifname, iface, sizeof(g_fullmac.ifname) - 1u);

	r = fl_net_wifi_nl80211_init(&g_fullmac.nl, g_fullmac.ifname);
	if (r != FL_RESULT_OK)
		return r;

	(void)fl_net_wifi_nl80211_get_wiphy_caps(g_fullmac.nl, &g_fullmac.phy_he,
						 &g_fullmac.bands);

#if defined(__linux__)
	if (fl_net_wifi_nl80211_ifindex(g_fullmac.nl, &ifindex) != FL_RESULT_OK ||
	    fullmac_open_packet_socket(g_fullmac.ifname, (unsigned int)ifindex,
				       &g_fullmac.pkt_fd) != FL_RESULT_OK) {
		fl_net_wifi_nl80211_deinit(g_fullmac.nl);
		g_fullmac.nl = NULL;
		return FL_RESULT_NOSYS;
	}
#endif

	memset(&g_fullmac.driver, 0, sizeof(g_fullmac.driver));
	g_fullmac.driver.send = fullmac_driver_send;
	g_fullmac.driver.recv = fullmac_driver_recv;
	g_fullmac.driver.mtu = FL_NET_ETH_MTU_DEFAULT;
	g_fullmac.driver.impl = &g_fullmac;

	return FL_RESULT_OK;
}

void fl_net_wifi_fullmac_deinit(void)
{
#if defined(__linux__)
	if (g_fullmac.pkt_fd >= 0) {
		close(g_fullmac.pkt_fd);
		g_fullmac.pkt_fd = -1;
	}
#endif
	if (g_fullmac.nl) {
		fl_net_wifi_nl80211_deinit(g_fullmac.nl);
		g_fullmac.nl = NULL;
	}
	g_fullmac.neg_he_valid = 0;
	g_fullmac.connected = 0;
	memset(&g_fullmac.driver, 0, sizeof(g_fullmac.driver));
}

int fl_net_wifi_fullmac_active(void)
{
	return g_fullmac.nl != NULL;
}

fl_net_driver_t *fl_net_wifi_fullmac_driver(void)
{
	return g_fullmac.nl ? &g_fullmac.driver : NULL;
}

const char *fl_net_wifi_fullmac_ifname(void)
{
	return g_fullmac.nl ? g_fullmac.ifname : NULL;
}

fl_result_t fl_net_wifi_fullmac_scan(uint8_t band, unsigned timeout_ms)
{
	fl_result_t r;

	(void)band;
	if (!g_fullmac.nl)
		return FL_RESULT_NOSYS;
	r = fl_net_wifi_nl80211_trigger_scan(g_fullmac.nl, NULL);
	if (r != FL_RESULT_OK)
		return r;
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
	if (!g_fullmac.nl)
		return FL_RESULT_NOSYS;

	r = fl_net_wifi_nl80211_get_scan(g_fullmac.nl, tmp,
					 cap < 64u ? cap : 64u, &count);
	if (r != FL_RESULT_OK && count == 0u)
		return r;

	*count_out = 0;
	for (i = 0; i < count && *count_out < cap; i++) {
		entries[*count_out] = tmp[i];
		(*count_out)++;
	}
	return *count_out > 0u ? FL_RESULT_OK : FL_RESULT_ERR;
}

fl_result_t fl_net_wifi_fullmac_disconnect(void)
{
	if (!g_fullmac.nl)
		return FL_RESULT_NOSYS;
	g_fullmac.connected = 0;
	g_fullmac.neg_he_valid = 0;
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_fullmac_he_cap(fl_net_wifi_he_cap_t *cap_out)
{
	if (!cap_out)
		return FL_RESULT_INVAL;
	if (!g_fullmac.nl)
		return FL_RESULT_NOSYS;
	if (g_fullmac.neg_he_valid) {
		*cap_out = g_fullmac.neg_he;
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
