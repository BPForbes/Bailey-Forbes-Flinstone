#ifndef WIFI_HOST_WIRE_H
#define WIFI_HOST_WIRE_H

/**
 * First-principles Linux hosted Wi-Fi wire constants and layouts (#328).
 *
 * Driver-layer UAPI byte layouts for netlink / genetlink / nl80211 / AF_PACKET.
 * No Linux kernel headers.
 */

#include <stdint.h>

/* ── Syscall address families / socket types (Linux ABI) ───────────────── */

#define FL_WIFI_WIRE_AF_NETLINK 16
#define FL_WIFI_WIRE_AF_PACKET 17
#define FL_WIFI_WIRE_SOCK_RAW 3
#define FL_WIFI_WIRE_NETLINK_GENERIC 16

/* Ethernet protocol IDs (network byte order on wire; htons for socket()). */
#define FL_WIFI_WIRE_ETH_P_ALL 0x0003u
#define FL_WIFI_WIRE_ETH_ALEN 6u

/* ── Netlink message layout ──────────────────────────────────────────── */

#define FL_WIFI_NLMSG_ALIGNTO 4u
#define FL_WIFI_NLMSG_ALIGN(len) (((len) + FL_WIFI_NLMSG_ALIGNTO - 1u) & ~(FL_WIFI_NLMSG_ALIGNTO - 1u))
#define FL_WIFI_NLMSG_HDRLEN ((int)FL_WIFI_NLMSG_ALIGN(sizeof(struct fl_wifi_nlmsghdr)))
#define FL_WIFI_NLMSG_LENGTH(len) ((len) + (unsigned int)FL_WIFI_NLMSG_HDRLEN)
#define FL_WIFI_NLMSG_SPACE(len) FL_WIFI_NLMSG_ALIGN(FL_WIFI_NLMSG_LENGTH(len))
#define FL_WIFI_NLMSG_DATA(nlh) ((void *)(((char *)(nlh)) + FL_WIFI_NLMSG_HDRLEN))
#define FL_WIFI_NLMSG_NEXT(nlh, len)                                                           \
	((len) -= (int)FL_WIFI_NLMSG_ALIGN((nlh)->nlmsg_len),                                  \
	 (struct fl_wifi_nlmsghdr *)(((char *)(nlh)) + FL_WIFI_NLMSG_ALIGN((nlh)->nlmsg_len)))
#define FL_WIFI_NLMSG_OK(nlh, len)                                                             \
	((len) >= (int)sizeof(struct fl_wifi_nlmsghdr) && (nlh)->nlmsg_len >=                    \
			sizeof(struct fl_wifi_nlmsghdr) && (nlh)->nlmsg_len <= (unsigned int)(len))

#define FL_WIFI_NLM_F_REQUEST 0x01u
#define FL_WIFI_NLM_F_ACK 0x04u
#define FL_WIFI_NLM_F_ROOT 0x0100u
#define FL_WIFI_NLM_F_MATCH 0x0200u
#define FL_WIFI_NLM_F_DUMP (FL_WIFI_NLM_F_ROOT | FL_WIFI_NLM_F_MATCH)

#define FL_WIFI_NLMSG_ERROR 0x02u
#define FL_WIFI_NLMSG_DONE 0x03u
#define FL_WIFI_NLMSG_MIN_TYPE 0x10u

struct fl_wifi_sockaddr_nl {
	uint16_t nl_family;
	uint16_t nl_pad;
	uint32_t nl_pid;
	uint32_t nl_groups;
};

struct fl_wifi_nlmsghdr {
	uint32_t nlmsg_len;
	uint16_t nlmsg_type;
	uint16_t nlmsg_flags;
	uint32_t nlmsg_seq;
	uint32_t nlmsg_pid;
};

struct fl_wifi_nlmsgerr {
	int error;
	struct fl_wifi_nlmsghdr msg;
};

/* ── Netlink attributes ──────────────────────────────────────────────── */

#define FL_WIFI_NLA_ALIGNTO 4
#define FL_WIFI_NLA_ALIGN(len) (((len) + FL_WIFI_NLA_ALIGNTO - 1) & ~(FL_WIFI_NLA_ALIGNTO - 1))
#define FL_WIFI_NLA_HDRLEN ((int)FL_WIFI_NLA_ALIGN(sizeof(struct fl_wifi_nlattr)))

struct fl_wifi_nlattr {
	uint16_t nla_len;
	uint16_t nla_type;
};

static inline int fl_wifi_nla_ok(const struct fl_wifi_nlattr *nla, int len)
{
	return len >= (int)sizeof(struct fl_wifi_nlattr) && nla->nla_len >= sizeof(struct fl_wifi_nlattr) &&
	       nla->nla_len <= (unsigned int)len;
}

static inline struct fl_wifi_nlattr *fl_wifi_nla_next(const struct fl_wifi_nlattr *nla, int *len)
{
	*len -= (int)FL_WIFI_NLA_ALIGN(nla->nla_len);
	return (struct fl_wifi_nlattr *)((char *)nla + FL_WIFI_NLA_ALIGN(nla->nla_len));
}

/* ── Generic netlink (controller) ────────────────────────────────────── */

#define FL_WIFI_GENL_ID_CTRL FL_WIFI_NLMSG_MIN_TYPE

struct fl_wifi_genlmsghdr {
	uint8_t cmd;
	uint8_t version;
	uint16_t reserved;
};

#define FL_WIFI_GENL_HDRLEN FL_WIFI_NLMSG_ALIGN(sizeof(struct fl_wifi_genlmsghdr))

#define FL_WIFI_CTRL_CMD_NEWFAMILY 1u
#define FL_WIFI_CTRL_CMD_GETFAMILY 3u

#define FL_WIFI_CTRL_ATTR_FAMILY_ID 1u
#define FL_WIFI_CTRL_ATTR_FAMILY_NAME 2u

/* ── nl80211 commands / attributes (Linux UAPI numeric IDs) ──────────── */

#define FL_WIFI_NL80211_CMD_GET_WIPHY 1u
#define FL_WIFI_NL80211_CMD_GET_INTERFACE 4u
#define FL_WIFI_NL80211_CMD_NEW_KEY 9u
#define FL_WIFI_NL80211_CMD_GET_SCAN 30u
#define FL_WIFI_NL80211_CMD_TRIGGER_SCAN 31u
#define FL_WIFI_NL80211_CMD_NEW_SCAN_RESULTS 32u
#define FL_WIFI_NL80211_CMD_REGISTER_FRAME 56u
#define FL_WIFI_NL80211_CMD_FRAME 57u
#define FL_WIFI_NL80211_CMD_FRAME_TX_STATUS 58u

#define FL_WIFI_NL80211_ATTR_WIPHY 2u
#define FL_WIFI_NL80211_ATTR_WIPHY_NAME 3u
#define FL_WIFI_NL80211_ATTR_IFINDEX 4u
#define FL_WIFI_NL80211_ATTR_IFNAME 5u
#define FL_WIFI_NL80211_ATTR_MAC 7u
#define FL_WIFI_NL80211_ATTR_KEY_DATA 8u
#define FL_WIFI_NL80211_ATTR_KEY_IDX 9u
#define FL_WIFI_NL80211_ATTR_KEY_CIPHER 10u
#define FL_WIFI_NL80211_ATTR_SCAN_SSIDS 45u
#define FL_WIFI_NL80211_ATTR_WIPHY_BANDS 23u
#define FL_WIFI_NL80211_ATTR_BSS 48u
#define FL_WIFI_NL80211_ATTR_FRAME 52u
#define FL_WIFI_NL80211_ATTR_KEY_TYPE 56u
#define FL_WIFI_NL80211_ATTR_COOKIE 89u
#define FL_WIFI_NL80211_ATTR_ACK 93u
#define FL_WIFI_NL80211_ATTR_FRAME_TYPE 102u

#define FL_WIFI_NL80211_BAND_ATTR_IFTYPE_DATA 8u

#define FL_WIFI_NL80211_BAND_IFTYPE_ATTR_HE_CAP_MAC 1u
#define FL_WIFI_NL80211_BAND_IFTYPE_ATTR_HE_CAP_PHY 2u

/* Linux UAPI enum nl80211_bss (1-based attribute IDs). */
#define FL_WIFI_NL80211_BSS_BSSID 1u
#define FL_WIFI_NL80211_BSS_FREQUENCY 2u
#define FL_WIFI_NL80211_BSS_INFORMATION_ELEMENTS 6u
#define FL_WIFI_NL80211_BSS_SIGNAL_MBM 7u

#define FL_WIFI_NL80211_KEYTYPE_GROUP 0u
#define FL_WIFI_NL80211_KEYTYPE_PAIRWISE 1u

#define FL_WIFI_WLAN_CIPHER_SUITE_CCMP 0x000fac04u

/* ── AF_PACKET link-layer sockaddr ─────────────────────────────────── */

struct fl_wifi_sockaddr_ll {
	uint16_t sll_family;
	uint16_t sll_protocol;
	int sll_ifindex;
	uint16_t sll_hatype;
	uint8_t sll_pkttype;
	uint8_t sll_halen;
	uint8_t sll_addr[8];
};

#endif /* WIFI_HOST_WIRE_H */
