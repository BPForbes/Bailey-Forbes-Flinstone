#include "wifi_nl80211.h"

#include "net_wifi_he.h"
#include "net_wifi_mgmt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)

#include "wifi_host_wire.h"

#define FL_NET_WIFI_IFNAME_MAX 16u

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define NL80211_MAX_MSG 8192u
#define NL80211_MGMT_RX_Q 8u
#define NL80211_MGMT_REG_MAX 8u

struct fl_net_wifi_nl80211 {
	int fd;
	int nl80211_id;
	uint32_t ifindex;
	uint32_t wiphy;
	char ifname[FL_NET_WIFI_IFNAME_MAX];
	uint8_t sta_mac[6];
	fl_net_wifi_he_cap_t phy_he;
	uint8_t bands;
	int mt7921;
	uint64_t last_cookie;
	int last_tx_ok;
	struct {
		uint16_t frame_type;
		fl_net_wifi_nl80211_mgmt_cb_t cb;
		void *ctx;
	} mgmt_reg[NL80211_MGMT_REG_MAX];
	unsigned mgmt_reg_count;
	uint8_t mgmt_rx[NL80211_MGMT_RX_Q][512];
	size_t mgmt_rx_len[NL80211_MGMT_RX_Q];
	unsigned mgmt_rx_head;
	unsigned mgmt_rx_count;
};

static int nl80211_enabled(void)
{
	const char *env = getenv("FL_NET_WIFI_NL80211");

	if (env && env[0] && strcmp(env, "0") != 0)
		return 1;
	if (getenv("FL_NET_WIFI_IFACE") && getenv("FL_NET_WIFI_IFACE")[0])
		return 1;
	return 0;
}

int fl_net_wifi_nl80211_available(void)
{
	return nl80211_enabled();
}

static int nla_put_u8(void *buf, size_t cap, size_t *pos, uint16_t type, uint8_t v)
{
	struct fl_wifi_nlattr *na;
	size_t need = FL_WIFI_NLA_HDRLEN + 1u;
	size_t aligned_need = (size_t)FL_WIFI_NLA_ALIGN((int)need);

	if (*pos + aligned_need > cap)
		return -1;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)need;
	memcpy((uint8_t *)na + FL_WIFI_NLA_HDRLEN, &v, 1u);
	memset((uint8_t *)na + need, 0, aligned_need - need);
	*pos += aligned_need;
	return 0;
}

static int nla_put_u16(void *buf, size_t cap, size_t *pos, uint16_t type, uint16_t v)
{
	struct fl_wifi_nlattr *na;
	size_t need = FL_WIFI_NLA_HDRLEN + 2u;
	size_t aligned_need = (size_t)FL_WIFI_NLA_ALIGN((int)need);

	if (*pos + aligned_need > cap)
		return -1;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)need;
	memcpy((uint8_t *)na + FL_WIFI_NLA_HDRLEN, &v, 2u);
	memset((uint8_t *)na + need, 0, aligned_need - need);
	*pos += aligned_need;
	return 0;
}

static int nla_put_u32(void *buf, size_t cap, size_t *pos, uint16_t type, uint32_t v)
{
	struct fl_wifi_nlattr *na;
	size_t need = FL_WIFI_NLA_HDRLEN + 4u;
	size_t aligned_need = (size_t)FL_WIFI_NLA_ALIGN((int)need);

	if (*pos + aligned_need > cap)
		return -1;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)need;
	memcpy((uint8_t *)na + FL_WIFI_NLA_HDRLEN, &v, 4u);
	memset((uint8_t *)na + need, 0, aligned_need - need);
	*pos += aligned_need;
	return 0;
}

static int nla_put_str(void *buf, size_t cap, size_t *pos, uint16_t type, const char *s)
{
	size_t slen;
	struct fl_wifi_nlattr *na;
	size_t need;
	size_t aligned_need;

	if (!s)
		return -1;
	slen = strlen(s) + 1u;
	need = FL_WIFI_NLA_HDRLEN + slen;
	aligned_need = (size_t)FL_WIFI_NLA_ALIGN((int)need);
	if (*pos + aligned_need > cap)
		return -1;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)need;
	memcpy((uint8_t *)na + FL_WIFI_NLA_HDRLEN, s, slen);
	memset((uint8_t *)na + need, 0, aligned_need - need);
	*pos += aligned_need;
	return 0;
}

static int nla_put_data(void *buf, size_t cap, size_t *pos, uint16_t type, const void *data,
			size_t len)
{
	struct fl_wifi_nlattr *na;
	size_t need = FL_WIFI_NLA_HDRLEN + len;
	size_t aligned_need = (size_t)FL_WIFI_NLA_ALIGN((int)need);

	if (*pos + aligned_need > cap)
		return -1;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)need;
	memcpy((uint8_t *)na + FL_WIFI_NLA_HDRLEN, data, len);
	memset((uint8_t *)na + need, 0, aligned_need - need);
	*pos += aligned_need;
	return 0;
}

static int nla_put_nested_begin(void *buf, size_t cap, size_t *pos, uint16_t type, size_t *nest_pos)
{
	struct fl_wifi_nlattr *na;

	if (*pos + (size_t)FL_WIFI_NLA_ALIGN(FL_WIFI_NLA_HDRLEN) > cap)
		return -1;
	*nest_pos = *pos;
	na = (struct fl_wifi_nlattr *)((uint8_t *)buf + *pos);
	na->nla_type = type;
	na->nla_len = (uint16_t)FL_WIFI_NLA_HDRLEN;
	*pos += (size_t)FL_WIFI_NLA_ALIGN(FL_WIFI_NLA_HDRLEN);
	return 0;
}

static void nla_put_nested_end(void *buf, size_t *pos, size_t nest_pos)
{
	struct fl_wifi_nlattr *na = (struct fl_wifi_nlattr *)((uint8_t *)buf + nest_pos);

	(void)buf;
	na->nla_len = (uint16_t)(*pos - nest_pos);
}

static int nla_get_u32(const struct fl_wifi_nlattr *attr, uint32_t *out)
{
	if (!attr || !out || attr->nla_len < FL_WIFI_NLA_HDRLEN + (int)sizeof(uint32_t))
		return -1;
	memcpy(out, (const uint8_t *)attr + FL_WIFI_NLA_HDRLEN, sizeof(*out));
	return 0;
}

static int nla_get_s32(const struct fl_wifi_nlattr *attr, int32_t *out)
{
	if (!attr || !out || attr->nla_len < FL_WIFI_NLA_HDRLEN + (int)sizeof(int32_t))
		return -1;
	memcpy(out, (const uint8_t *)attr + FL_WIFI_NLA_HDRLEN, sizeof(*out));
	return 0;
}

static int nl80211_send(fl_net_wifi_nl80211_t *nl, void *buf, size_t len)
{
	struct fl_wifi_sockaddr_nl sa;
	struct msghdr msg;
	struct iovec iov;

	if (!nl || nl->fd < 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = FL_WIFI_WIRE_AF_NETLINK;
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = buf;
	iov.iov_len = len;
	msg.msg_name = &sa;
	msg.msg_namelen = sizeof(sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	return (int)sendmsg(nl->fd, &msg, 0);
}

static int nl80211_recv(fl_net_wifi_nl80211_t *nl, void *buf, size_t cap, unsigned timeout_ms)
{
	struct pollfd pfd;
	int pr;

	if (!nl || nl->fd < 0)
		return -1;
	pfd.fd = nl->fd;
	pfd.events = POLLIN;
	pr = poll(&pfd, 1, (int)timeout_ms);
	if (pr <= 0)
		return -1;
	return (int)recv(nl->fd, buf, cap, 0);
}

static fl_result_t nl80211_wait_ack(fl_net_wifi_nl80211_t *nl, unsigned timeout_ms)
{
	uint8_t buf[NL80211_MAX_MSG];
	int n;
	struct fl_wifi_nlmsghdr *nh;

	n = nl80211_recv(nl, buf, sizeof(buf), timeout_ms ? timeout_ms : 2000u);
	if (n < 0)
		return FL_RESULT_TIMEDOUT;
	nh = (struct fl_wifi_nlmsghdr *)buf;
	while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
		if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR) {
			struct fl_wifi_nlmsgerr *err = (struct fl_wifi_nlmsgerr *)FL_WIFI_NLMSG_DATA(nh);

			return err->error == 0 ? FL_RESULT_OK : FL_RESULT_ERR;
		}
		nh = FL_WIFI_NLMSG_NEXT(nh, n);
	}
	return FL_RESULT_ERR;
}

static fl_result_t nl80211_send_and_ack(fl_net_wifi_nl80211_t *nl, void *req, size_t req_len,
					unsigned timeout_ms)
{
	if (nl80211_send(nl, req, req_len) < 0)
		return FL_RESULT_ERR;
	return nl80211_wait_ack(nl, timeout_ms);
}

static int nl80211_build_msg(void *buf, size_t cap, uint16_t nlmsg_type, uint8_t genl_cmd,
			     uint16_t flags, uint32_t seq, size_t *attr_pos)
{
	struct fl_wifi_nlmsghdr *nh;
	struct fl_wifi_genlmsghdr *gh;

	if (cap < FL_WIFI_NLMSG_SPACE(FL_WIFI_GENL_HDRLEN))
		return -1;
	memset(buf, 0, FL_WIFI_NLMSG_SPACE(FL_WIFI_GENL_HDRLEN));
	nh = (struct fl_wifi_nlmsghdr *)buf;
	nh->nlmsg_type = nlmsg_type;
	nh->nlmsg_flags = (uint16_t)(flags | FL_WIFI_NLM_F_ACK);
	nh->nlmsg_seq = seq;
	nh->nlmsg_pid = 0;
	gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
	gh->cmd = genl_cmd;
	gh->version = 1;
	*attr_pos = FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN);
	return 0;
}

static void nl80211_finish_msg(void *buf, size_t attr_pos)
{
	struct fl_wifi_nlmsghdr *nh = (struct fl_wifi_nlmsghdr *)buf;

	nh->nlmsg_len = (uint32_t)FL_WIFI_NLMSG_ALIGN(attr_pos);
}

static int nl80211_resolve_family(fl_net_wifi_nl80211_t *nl)
{
	uint8_t buf[NL80211_MAX_MSG];
	struct fl_wifi_nlmsghdr *nh;
	struct fl_wifi_genlmsghdr *gh;
	size_t pos;
	int n;
	uint16_t seq = 1;

	if (nl80211_build_msg(buf, sizeof(buf), FL_WIFI_GENL_ID_CTRL, FL_WIFI_CTRL_CMD_GETFAMILY, FL_WIFI_NLM_F_REQUEST,
			      seq, &pos) != 0)
		return -1;
	if (nla_put_str(buf, sizeof(buf), &pos, FL_WIFI_CTRL_ATTR_FAMILY_NAME, "nl80211") != 0)
		return -1;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send(nl, buf, pos) < 0)
		return -1;
	n = nl80211_recv(nl, buf, sizeof(buf), 2000u);
	if (n < 0)
		return -1;
	nh = (struct fl_wifi_nlmsghdr *)buf;
	while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
		struct fl_wifi_nlattr *attr;
		int rem;

		if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR)
			return -1;
		gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
		if (gh->cmd != FL_WIFI_CTRL_CMD_NEWFAMILY) {
			nh = FL_WIFI_NLMSG_NEXT(nh, n);
			continue;
		}
		attr = (struct fl_wifi_nlattr *)((uint8_t *)gh + FL_WIFI_GENL_HDRLEN);
		rem = (int)(nh->nlmsg_len - FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN));
		while (fl_wifi_nla_ok(attr, rem)) {
			if (attr->nla_type == FL_WIFI_CTRL_ATTR_FAMILY_ID) {
				int32_t family_id = 0;

				if (nla_get_s32(attr, &family_id) != 0)
					return -1;
				nl->nl80211_id = family_id;
				return 0;
			}
			attr = fl_wifi_nla_next(attr, &rem);
		}
		nh = FL_WIFI_NLMSG_NEXT(nh, n);
	}
	return -1;
}

static int nl80211_get_interface(fl_net_wifi_nl80211_t *nl, const char *ifname)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	int n;
	struct fl_wifi_nlmsghdr *nh;
	struct fl_wifi_genlmsghdr *gh;
	static uint32_t seq;

	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id,
			      FL_WIFI_NL80211_CMD_GET_INTERFACE, FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return -1;
	if (nla_put_str(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFNAME, ifname) != 0)
		return -1;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send(nl, buf, pos) < 0)
		return -1;
	n = nl80211_recv(nl, buf, sizeof(buf), 2000u);
	if (n < 0)
		return -1;
	nh = (struct fl_wifi_nlmsghdr *)buf;
	while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
		struct fl_wifi_nlattr *attr;
		int rem;

		if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR)
			return -1;
		gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
		attr = (struct fl_wifi_nlattr *)((uint8_t *)gh + FL_WIFI_GENL_HDRLEN);
		rem = (int)(nh->nlmsg_len - FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN));
		while (fl_wifi_nla_ok(attr, rem)) {
			if (attr->nla_type == FL_WIFI_NL80211_ATTR_IFINDEX) {
				uint32_t ifindex = 0;

				if (nla_get_u32(attr, &ifindex) == 0)
					nl->ifindex = ifindex;
			} else if (attr->nla_type == FL_WIFI_NL80211_ATTR_WIPHY) {
				uint32_t wiphy = 0;

				if (nla_get_u32(attr, &wiphy) == 0)
					nl->wiphy = wiphy;
			} else if (attr->nla_type == FL_WIFI_NL80211_ATTR_MAC)
				memcpy(nl->sta_mac, (uint8_t *)attr + FL_WIFI_NLA_HDRLEN, 6u);
			attr = fl_wifi_nla_next(attr, &rem);
		}
		if (nl->ifindex)
			return 0;
		nh = FL_WIFI_NLMSG_NEXT(nh, n);
	}
	return -1;
}

static void nl80211_parse_band_he(struct fl_wifi_nlattr *band, fl_net_wifi_nl80211_t *nl, unsigned band_idx)
{
	struct fl_wifi_nlattr *attr;
	int rem;

	if (!band || !nl)
		return;
	if (band_idx == 0u)
		nl->bands |= FL_WIFI_BAND_2GHZ;
	else if (band_idx == 1u)
		nl->bands |= FL_WIFI_BAND_5GHZ;
	else
		nl->bands |= FL_WIFI_BAND_6GHZ;
	attr = (struct fl_wifi_nlattr *)((uint8_t *)band + FL_WIFI_NLA_HDRLEN);
	rem = (int)(band->nla_len - FL_WIFI_NLA_HDRLEN);
	while (fl_wifi_nla_ok(attr, rem)) {
		if (attr->nla_type == FL_WIFI_NL80211_BAND_ATTR_IFTYPE_DATA) {
			struct fl_wifi_nlattr *ift;
			int ift_rem;

			ift = (struct fl_wifi_nlattr *)((uint8_t *)attr + FL_WIFI_NLA_HDRLEN);
			ift_rem = (int)(attr->nla_len - FL_WIFI_NLA_HDRLEN);
			while (fl_wifi_nla_ok(ift, ift_rem)) {
				if (ift->nla_type == FL_WIFI_NL80211_BAND_IFTYPE_ATTR_HE_CAP_MAC) {
					const uint8_t *mac =
						(const uint8_t *)ift + FL_WIFI_NLA_HDRLEN;
					size_t mac_len = ift->nla_len - FL_WIFI_NLA_HDRLEN;

					if (mac_len >= 4u)
						nl->phy_he.supports_twt =
							((mac[3] & 0x06u) != 0u) ? 1u : 0u;
					nl->phy_he.supports_ofdma = 1u;
				} else if (ift->nla_type == FL_WIFI_NL80211_BAND_IFTYPE_ATTR_HE_CAP_PHY) {
					const uint8_t *phy =
						(const uint8_t *)ift + FL_WIFI_NLA_HDRLEN;
					size_t phy_len = ift->nla_len - FL_WIFI_NLA_HDRLEN;

					if (phy_len >= 1u) {
						if (phy[0] & 0x40u)
							nl->phy_he.channel_width_mhz = 160u;
						else if (phy[0] & 0x04u)
							nl->phy_he.channel_width_mhz = 80u;
						else if (phy[0] & 0x02u)
							nl->phy_he.channel_width_mhz = 40u;
						else
							nl->phy_he.channel_width_mhz = 20u;
						if (phy[0] & 0x20u)
							nl->phy_he.supports_6ghz = 1u;
					}
				}
				ift = fl_wifi_nla_next(ift, &ift_rem);
			}
		}
		attr = fl_wifi_nla_next(attr, &rem);
	}
}

static int nl80211_get_wiphy(fl_net_wifi_nl80211_t *nl)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	int n;
	struct fl_wifi_nlmsghdr *nh;
	struct fl_wifi_genlmsghdr *gh;
	static uint32_t seq;

	if (!nl->wiphy)
		return -1;
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id, FL_WIFI_NL80211_CMD_GET_WIPHY,
			      FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return -1;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_WIPHY, nl->wiphy) != 0)
		return -1;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send(nl, buf, pos) < 0)
		return -1;
	n = nl80211_recv(nl, buf, sizeof(buf), 2000u);
	if (n < 0)
		return -1;
	nh = (struct fl_wifi_nlmsghdr *)buf;
	while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
		struct fl_wifi_nlattr *attr;
		int rem;

		if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR)
			return -1;
		gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
		attr = (struct fl_wifi_nlattr *)((uint8_t *)gh + FL_WIFI_GENL_HDRLEN);
		rem = (int)(nh->nlmsg_len - FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN));
		while (fl_wifi_nla_ok(attr, rem)) {
			if (attr->nla_type == FL_WIFI_NL80211_ATTR_WIPHY_NAME) {
				const char *name = (const char *)((uint8_t *)attr + FL_WIFI_NLA_HDRLEN);

				if (strstr(name, "7921") != NULL)
					nl->mt7921 = 1;
			} else if (attr->nla_type == FL_WIFI_NL80211_ATTR_WIPHY_BANDS) {
				struct fl_wifi_nlattr *band;
				int band_rem;
				unsigned band_idx = 0;

				band = (struct fl_wifi_nlattr *)((uint8_t *)attr + FL_WIFI_NLA_HDRLEN);
				band_rem = (int)(attr->nla_len - FL_WIFI_NLA_HDRLEN);
				while (fl_wifi_nla_ok(band, band_rem)) {
					nl80211_parse_band_he(band, nl, band_idx);
					band_idx++;
					band = fl_wifi_nla_next(band, &band_rem);
				}
			}
			attr = fl_wifi_nla_next(attr, &rem);
		}
		return 0;
	}
	return -1;
}

static int nl80211_mgmt_enqueue(fl_net_wifi_nl80211_t *nl, const uint8_t *frame, size_t len)
{
	unsigned idx;

	if (!nl || !frame || len == 0 || len > 512u)
		return -1;
	if (nl->mgmt_rx_count >= NL80211_MGMT_RX_Q)
		return -1;
	idx = (nl->mgmt_rx_head + nl->mgmt_rx_count) % NL80211_MGMT_RX_Q;
	memcpy(nl->mgmt_rx[idx], frame, len);
	nl->mgmt_rx_len[idx] = len;
	nl->mgmt_rx_count++;
	return 0;
}

static void nl80211_dispatch_mgmt(fl_net_wifi_nl80211_t *nl, const uint8_t *frame, size_t len)
{
	uint16_t fc;
	unsigned i;

	if (!nl || !frame || len < 2u)
		return;
	fc = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
	for (i = 0; i < nl->mgmt_reg_count; i++) {
		if (nl->mgmt_reg[i].frame_type == fc && nl->mgmt_reg[i].cb)
			nl->mgmt_reg[i].cb(frame, len, nl->mgmt_reg[i].ctx);
	}
	(void)nl80211_mgmt_enqueue(nl, frame, len);
}

static void nl80211_handle_msg(fl_net_wifi_nl80211_t *nl, struct fl_wifi_nlmsghdr *nh)
{
	struct fl_wifi_genlmsghdr *gh;
	struct fl_wifi_nlattr *attr;
	int rem;

	if (!nl || !nh)
		return;
	if (nh->nlmsg_type != (unsigned int)nl->nl80211_id)
		return;
	gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
	attr = (struct fl_wifi_nlattr *)((uint8_t *)gh + FL_WIFI_GENL_HDRLEN);
	rem = (int)(nh->nlmsg_len - FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN));
	if (gh->cmd == FL_WIFI_NL80211_CMD_FRAME) {
		const uint8_t *frame = NULL;
		size_t frame_len = 0;

		while (fl_wifi_nla_ok(attr, rem)) {
			if (attr->nla_type == FL_WIFI_NL80211_ATTR_FRAME) {
				frame = (const uint8_t *)attr + FL_WIFI_NLA_HDRLEN;
				frame_len = attr->nla_len - FL_WIFI_NLA_HDRLEN;
			}
			attr = fl_wifi_nla_next(attr, &rem);
		}
		if (frame && frame_len > 0u)
			nl80211_dispatch_mgmt(nl, frame, frame_len);
	} else if (gh->cmd == FL_WIFI_NL80211_CMD_FRAME_TX_STATUS) {
		uint64_t cookie = 0;
		int ack = 0;

		while (fl_wifi_nla_ok(attr, rem)) {
			if (attr->nla_type == FL_WIFI_NL80211_ATTR_COOKIE)
				memcpy(&cookie, (uint8_t *)attr + FL_WIFI_NLA_HDRLEN,
				       attr->nla_len - FL_WIFI_NLA_HDRLEN);
			else if (attr->nla_type == FL_WIFI_NL80211_ATTR_ACK)
				ack = 1;
			attr = fl_wifi_nla_next(attr, &rem);
		}
		if (cookie == nl->last_cookie)
			nl->last_tx_ok = ack;
	}
}

fl_result_t fl_net_wifi_nl80211_init(fl_net_wifi_nl80211_t **out, const char *ifname)
{
	fl_net_wifi_nl80211_t *nl;
	const char *iface;
	struct fl_wifi_sockaddr_nl sa;

	if (!out || !ifname || !ifname[0])
		return FL_RESULT_INVAL;
	if (!nl80211_enabled())
		return FL_RESULT_NOSYS;

	nl = (fl_net_wifi_nl80211_t *)calloc(1, sizeof(*nl));
	if (!nl)
		return FL_RESULT_ERR;
	nl->fd = socket(FL_WIFI_WIRE_AF_NETLINK, FL_WIFI_WIRE_SOCK_RAW, FL_WIFI_WIRE_NETLINK_GENERIC);
	if (nl->fd < 0) {
		free(nl);
		return FL_RESULT_NOSYS;
	}
	memset(&sa, 0, sizeof(sa));
	sa.nl_family = FL_WIFI_WIRE_AF_NETLINK;
	sa.nl_groups = 0;
	if (bind(nl->fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(nl->fd);
		free(nl);
		return FL_RESULT_NOSYS;
	}
	if (nl80211_resolve_family(nl) != 0) {
		close(nl->fd);
		free(nl);
		return FL_RESULT_NOSYS;
	}
	iface = ifname;
	if (!iface[0]) {
		const char *env = getenv("FL_NET_WIFI_IFACE");

		iface = (env && env[0]) ? env : "wlan0";
	}
	strncpy(nl->ifname, iface, sizeof(nl->ifname) - 1u);
	if (nl80211_get_interface(nl, nl->ifname) != 0) {
		close(nl->fd);
		free(nl);
		return FL_RESULT_NOSYS;
	}
	(void)nl80211_get_wiphy(nl);
	if (nl->phy_he.channel_width_mhz == 0u)
		nl->phy_he.channel_width_mhz = 80u;
	nl->phy_he.max_nss_rx = 2u;
	nl->phy_he.max_nss_tx = 2u;
	*out = nl;
	return FL_RESULT_OK;
}

void fl_net_wifi_nl80211_deinit(fl_net_wifi_nl80211_t *nl)
{
	if (!nl)
		return;
	if (nl->fd >= 0)
		close(nl->fd);
	free(nl);
}

fl_result_t fl_net_wifi_nl80211_ifindex(const fl_net_wifi_nl80211_t *nl, uint32_t *ifindex_out)
{
	if (!nl || !ifindex_out)
		return FL_RESULT_INVAL;
	if (!nl->ifindex)
		return FL_RESULT_ERR;
	*ifindex_out = nl->ifindex;
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_nl80211_sta_mac(const fl_net_wifi_nl80211_t *nl, uint8_t mac_out[6])
{
	if (!nl || !mac_out)
		return FL_RESULT_INVAL;
	memcpy(mac_out, nl->sta_mac, 6u);
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_nl80211_mgmt_tx(fl_net_wifi_nl80211_t *nl, const uint8_t *frame,
					size_t len, unsigned timeout_ms)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	struct fl_wifi_nlmsghdr *nh;
	static uint32_t seq;
	int n;

	if (!nl || !frame || len == 0)
		return FL_RESULT_INVAL;
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id, FL_WIFI_NL80211_CMD_FRAME,
			      FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return FL_RESULT_ERR;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFINDEX, nl->ifindex) != 0 ||
	    nla_put_data(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_FRAME, frame, len) != 0)
		return FL_RESULT_ERR;
	nl->last_cookie = (uint64_t)seq;
	nl->last_tx_ok = 0;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send(nl, buf, pos) < 0)
		return FL_RESULT_ERR;
	for (;;) {
		n = nl80211_recv(nl, buf, sizeof(buf), timeout_ms ? timeout_ms : 100u);
		if (n < 0)
			return FL_RESULT_TIMEDOUT;
		nh = (struct fl_wifi_nlmsghdr *)buf;
		while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
			if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR) {
				struct fl_wifi_nlmsgerr *err =
					(struct fl_wifi_nlmsgerr *)FL_WIFI_NLMSG_DATA(nh);

				if (err->error != 0)
					return FL_RESULT_ERR;
			} else {
				nl80211_handle_msg(nl, nh);
			}
			nh = FL_WIFI_NLMSG_NEXT(nh, n);
		}
		if (nl->last_tx_ok)
			return FL_RESULT_OK;
	}
	return FL_RESULT_TIMEDOUT;
}

fl_result_t fl_net_wifi_nl80211_register_mgmt(fl_net_wifi_nl80211_t *nl, uint16_t frame_type,
					     fl_net_wifi_nl80211_mgmt_cb_t cb, void *ctx)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	static uint32_t seq;

	if (!nl || !cb)
		return FL_RESULT_INVAL;
	if (nl->mgmt_reg_count >= NL80211_MGMT_REG_MAX)
		return FL_RESULT_ERR;
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id,
			      FL_WIFI_NL80211_CMD_REGISTER_FRAME, FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return FL_RESULT_ERR;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFINDEX, nl->ifindex) != 0 ||
	    nla_put_u16(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_FRAME_TYPE, frame_type) != 0)
		return FL_RESULT_ERR;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send_and_ack(nl, buf, pos, 2000u) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	nl->mgmt_reg[nl->mgmt_reg_count].frame_type = frame_type;
	nl->mgmt_reg[nl->mgmt_reg_count].cb = cb;
	nl->mgmt_reg[nl->mgmt_reg_count].ctx = ctx;
	nl->mgmt_reg_count++;
	return FL_RESULT_OK;
}

void fl_net_wifi_nl80211_unregister_mgmt_ctx(fl_net_wifi_nl80211_t *nl, void *ctx)
{
	unsigned i = 0;

	if (!nl || !ctx)
		return;
	while (i < nl->mgmt_reg_count) {
		if (nl->mgmt_reg[i].ctx != ctx) {
			i++;
			continue;
		}
		if (i + 1u < nl->mgmt_reg_count)
			memmove(&nl->mgmt_reg[i], &nl->mgmt_reg[i + 1u],
				(nl->mgmt_reg_count - i - 1u) * sizeof(nl->mgmt_reg[0]));
		nl->mgmt_reg_count--;
	}
}

fl_result_t fl_net_wifi_nl80211_poll(fl_net_wifi_nl80211_t *nl, unsigned timeout_ms)
{
	uint8_t buf[NL80211_MAX_MSG];
	int n;
	struct fl_wifi_nlmsghdr *nh;

	if (!nl)
		return FL_RESULT_INVAL;
	n = nl80211_recv(nl, buf, sizeof(buf), timeout_ms);
	if (n < 0)
		return FL_RESULT_TIMEDOUT;
	nh = (struct fl_wifi_nlmsghdr *)buf;
	while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
		nl80211_handle_msg(nl, nh);
		nh = FL_WIFI_NLMSG_NEXT(nh, n);
	}
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_nl80211_mgmt_rx(fl_net_wifi_nl80211_t *nl, uint8_t *frame, size_t cap,
					size_t *len_out, unsigned timeout_ms)
{
	if (!nl || !frame || !len_out)
		return FL_RESULT_INVAL;
	if (nl->mgmt_rx_count == 0) {
		fl_result_t rc = fl_net_wifi_nl80211_poll(nl, timeout_ms);

		if (rc != FL_RESULT_OK && nl->mgmt_rx_count == 0)
			return FL_RESULT_TIMEDOUT;
	}
	if (nl->mgmt_rx_count == 0)
		return FL_RESULT_TIMEDOUT;
	if (cap < nl->mgmt_rx_len[nl->mgmt_rx_head])
		return FL_RESULT_ERR;
	*len_out = nl->mgmt_rx_len[nl->mgmt_rx_head];
	memcpy(frame, nl->mgmt_rx[nl->mgmt_rx_head], *len_out);
	nl->mgmt_rx_head = (nl->mgmt_rx_head + 1u) % NL80211_MGMT_RX_Q;
	nl->mgmt_rx_count--;
	return FL_RESULT_OK;
}

static int nl80211_scan_bss(fl_net_wifi_scan_entry_t *entry, struct fl_wifi_nlattr *bss)
{
	struct fl_wifi_nlattr *attr;
	int rem;
	const uint8_t *ies = NULL;
	size_t ies_len = 0;
	int8_t rssi = -127;

	memset(entry, 0, sizeof(*entry));
	attr = (struct fl_wifi_nlattr *)((uint8_t *)bss + FL_WIFI_NLA_HDRLEN);
	rem = (int)(bss->nla_len - FL_WIFI_NLA_HDRLEN);
	while (fl_wifi_nla_ok(attr, rem)) {
		if (attr->nla_type == FL_WIFI_NL80211_BSS_INFORMATION_ELEMENTS) {
			ies = (const uint8_t *)attr + FL_WIFI_NLA_HDRLEN;
			ies_len = attr->nla_len - FL_WIFI_NLA_HDRLEN;
		} else if (attr->nla_type == FL_WIFI_NL80211_BSS_SIGNAL_MBM) {
			int32_t mbm = 0;

			if (nla_get_s32(attr, &mbm) == 0)
				rssi = (int8_t)(mbm / 100);
		} else if (attr->nla_type == FL_WIFI_NL80211_BSS_BSSID)
			memcpy(entry->bssid, (uint8_t *)attr + FL_WIFI_NLA_HDRLEN, 6u);
		else if (attr->nla_type == FL_WIFI_NL80211_BSS_FREQUENCY) {
			uint32_t freq = 0;

			if (nla_get_u32(attr, &freq) == 0) {
				if (freq < 3000u)
					entry->band = FL_WIFI_BAND_2GHZ;
				else if (freq < 5925u)
					entry->band = FL_WIFI_BAND_5GHZ;
				else
					entry->band = FL_WIFI_BAND_6GHZ;
			}
		}
		attr = fl_wifi_nla_next(attr, &rem);
	}
	entry->rssi_dbm = rssi;
	if (ies && ies_len > 0u)
		(void)fl_net_wifi_mgmt_enrich_scan_from_ies(ies, ies_len, entry);
	{
		static const uint8_t zero_bssid[6] = {0};

		return entry->ssid[0] != '\0' ||
		       memcmp(entry->bssid, zero_bssid, sizeof(entry->bssid)) != 0;
	}
}

fl_result_t fl_net_wifi_nl80211_trigger_scan(fl_net_wifi_nl80211_t *nl, const char *ssid)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	size_t ssid_len = 0;
	static uint32_t seq;

	if (!nl)
		return FL_RESULT_INVAL;
	if (ssid) {
		if (fl_net_wifi_scan_ssid_validate(ssid, &ssid_len) != FL_RESULT_OK)
			return FL_RESULT_INVAL;
	}
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id,
			      FL_WIFI_NL80211_CMD_TRIGGER_SCAN, FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return FL_RESULT_ERR;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFINDEX, nl->ifindex) != 0)
		return FL_RESULT_ERR;
	if (ssid && ssid_len > 0u) {
		size_t nest_pos = 0;

		if (nla_put_nested_begin(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_SCAN_SSIDS,
					 &nest_pos) != 0)
			return FL_RESULT_ERR;
		if (nla_put_data(buf, sizeof(buf), &pos, 1u, ssid, ssid_len) != 0)
			return FL_RESULT_ERR;
		nla_put_nested_end(buf, &pos, nest_pos);
	}
	nl80211_finish_msg(buf, pos);
	if (nl80211_send_and_ack(nl, buf, pos, 3000u) != FL_RESULT_OK)
		return FL_RESULT_ERR;
	(void)fl_net_wifi_nl80211_poll(nl, 3000u);
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_nl80211_get_scan(fl_net_wifi_nl80211_t *nl,
					 fl_net_wifi_scan_entry_t *entries, size_t cap,
					 size_t *count_out)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	int n;
	struct fl_wifi_nlmsghdr *nh;
	struct fl_wifi_genlmsghdr *gh;
	static uint32_t seq;
	size_t count = 0;

	if (!nl || !entries || !count_out || cap == 0u)
		return FL_RESULT_INVAL;
	*count_out = 0;
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id, FL_WIFI_NL80211_CMD_GET_SCAN,
			      FL_WIFI_NLM_F_DUMP, ++seq, &pos) != 0)
		return FL_RESULT_ERR;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFINDEX, nl->ifindex) != 0)
		return FL_RESULT_ERR;
	nl80211_finish_msg(buf, pos);
	if (nl80211_send(nl, buf, pos) < 0)
		return FL_RESULT_ERR;
	for (;;) {
		n = nl80211_recv(nl, buf, sizeof(buf), 500u);
		if (n < 0)
			break;
		nh = (struct fl_wifi_nlmsghdr *)buf;
		while (FL_WIFI_NLMSG_OK(nh, (unsigned int)n)) {
			struct fl_wifi_nlattr *attr;
			int rem;

			if (nh->nlmsg_type == FL_WIFI_NLMSG_DONE)
				goto done;
			if (nh->nlmsg_type == FL_WIFI_NLMSG_ERROR)
				return FL_RESULT_ERR;
			gh = (struct fl_wifi_genlmsghdr *)FL_WIFI_NLMSG_DATA(nh);
			if (gh->cmd != FL_WIFI_NL80211_CMD_NEW_SCAN_RESULTS &&
			    gh->cmd != FL_WIFI_NL80211_CMD_GET_SCAN)
				goto next;
			attr = (struct fl_wifi_nlattr *)((uint8_t *)gh + FL_WIFI_GENL_HDRLEN);
			rem = (int)(nh->nlmsg_len - FL_WIFI_NLMSG_LENGTH(FL_WIFI_GENL_HDRLEN));
			while (fl_wifi_nla_ok(attr, rem)) {
				if (attr->nla_type == FL_WIFI_NL80211_ATTR_BSS && count < cap) {
					if (nl80211_scan_bss(&entries[count], attr))
						count++;
				}
				attr = fl_wifi_nla_next(attr, &rem);
			}
		next:
			nh = FL_WIFI_NLMSG_NEXT(nh, n);
		}
	}
done:
	*count_out = count;
	return count > 0u ? FL_RESULT_OK : FL_RESULT_ERR;
}

fl_result_t fl_net_wifi_nl80211_get_wiphy_caps(fl_net_wifi_nl80211_t *nl,
					      fl_net_wifi_he_cap_t *he_out, uint8_t *bands_out)
{
	if (!nl || !he_out)
		return FL_RESULT_INVAL;
	*he_out = nl->phy_he;
	if (bands_out)
		*bands_out = nl->bands;
	return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_nl80211_install_key(fl_net_wifi_nl80211_t *nl, uint8_t key_index,
					    const uint8_t *key, size_t key_len, int pairwise)
{
	uint8_t buf[NL80211_MAX_MSG];
	size_t pos;
	static uint32_t seq;

	if (!nl || !key || key_len < 16u)
		return FL_RESULT_INVAL;
	if (nl80211_build_msg(buf, sizeof(buf), (uint16_t)nl->nl80211_id, FL_WIFI_NL80211_CMD_NEW_KEY,
			      FL_WIFI_NLM_F_REQUEST, ++seq, &pos) != 0)
		return FL_RESULT_ERR;
	if (nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_IFINDEX, nl->ifindex) != 0 ||
	    nla_put_u8(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_KEY_IDX, key_index) != 0 ||
	    nla_put_data(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_KEY_DATA, key, key_len) != 0 ||
	    nla_put_u32(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_KEY_CIPHER,
			FL_WIFI_WLAN_CIPHER_SUITE_CCMP) != 0 ||
	    nla_put_u8(buf, sizeof(buf), &pos, FL_WIFI_NL80211_ATTR_KEY_TYPE,
		       pairwise ? FL_WIFI_NL80211_KEYTYPE_PAIRWISE : FL_WIFI_NL80211_KEYTYPE_GROUP) != 0)
		return FL_RESULT_ERR;
	nl80211_finish_msg(buf, pos);
	return nl80211_send_and_ack(nl, buf, pos, 2000u);
}

int fl_net_wifi_nl80211_mt7921_detected(const fl_net_wifi_nl80211_t *nl)
{
	if (!nl)
		return 0;
	return nl->mt7921 ? 1 : 0;
}

#else /* !__linux__ */

struct fl_net_wifi_nl80211 {
	int unused;
};

int fl_net_wifi_nl80211_available(void)
{
	return 0;
}

fl_result_t fl_net_wifi_nl80211_init(fl_net_wifi_nl80211_t **out, const char *ifname)
{
	(void)ifname;
	if (!out)
		return FL_RESULT_INVAL;
	return FL_RESULT_NOSYS;
}

void fl_net_wifi_nl80211_deinit(fl_net_wifi_nl80211_t *nl)
{
	(void)nl;
}

fl_result_t fl_net_wifi_nl80211_ifindex(const fl_net_wifi_nl80211_t *nl, uint32_t *ifindex_out)
{
	(void)nl;
	(void)ifindex_out;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_sta_mac(const fl_net_wifi_nl80211_t *nl, uint8_t mac_out[6])
{
	(void)nl;
	(void)mac_out;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_mgmt_tx(fl_net_wifi_nl80211_t *nl, const uint8_t *frame,
					size_t len, unsigned timeout_ms)
{
	(void)nl;
	(void)frame;
	(void)len;
	(void)timeout_ms;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_register_mgmt(fl_net_wifi_nl80211_t *nl, uint16_t frame_type,
					     fl_net_wifi_nl80211_mgmt_cb_t cb, void *ctx)
{
	(void)nl;
	(void)frame_type;
	(void)cb;
	(void)ctx;
	return FL_RESULT_NOSYS;
}

void fl_net_wifi_nl80211_unregister_mgmt_ctx(fl_net_wifi_nl80211_t *nl, void *ctx)
{
	(void)nl;
	(void)ctx;
}

fl_result_t fl_net_wifi_nl80211_mgmt_rx(fl_net_wifi_nl80211_t *nl, uint8_t *frame, size_t cap,
					size_t *len_out, unsigned timeout_ms)
{
	(void)nl;
	(void)frame;
	(void)cap;
	(void)len_out;
	(void)timeout_ms;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_poll(fl_net_wifi_nl80211_t *nl, unsigned timeout_ms)
{
	(void)nl;
	(void)timeout_ms;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_trigger_scan(fl_net_wifi_nl80211_t *nl, const char *ssid)
{
	(void)nl;
	(void)ssid;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_get_scan(fl_net_wifi_nl80211_t *nl,
					 fl_net_wifi_scan_entry_t *entries, size_t cap,
					 size_t *count_out)
{
	(void)nl;
	(void)entries;
	(void)cap;
	(void)count_out;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_get_wiphy_caps(fl_net_wifi_nl80211_t *nl,
					      fl_net_wifi_he_cap_t *he_out, uint8_t *bands_out)
{
	(void)nl;
	(void)he_out;
	(void)bands_out;
	return FL_RESULT_NOSYS;
}

fl_result_t fl_net_wifi_nl80211_install_key(fl_net_wifi_nl80211_t *nl, uint8_t key_index,
					    const uint8_t *key, size_t key_len, int pairwise)
{
	(void)nl;
	(void)key_index;
	(void)key;
	(void)key_len;
	(void)pairwise;
	return FL_RESULT_NOSYS;
}

int fl_net_wifi_nl80211_mt7921_detected(const fl_net_wifi_nl80211_t *nl)
{
	(void)nl;
	return 0;
}

#endif /* __linux__ */
