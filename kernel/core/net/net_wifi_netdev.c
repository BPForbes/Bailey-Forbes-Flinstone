#include "net_wifi_netdev.h"

#include "contract_p3_ipv4.h"
#include "contract_p3_udp.h"
#include "contract_p3_wire.h"
#include "net_arp.h"
#include "net_checksum.h"
#include "net_dhcp.h"
#include "net_iface.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_route.h"
#include "net_udp.h"
#include "net_wire.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FL_NET_WIFI_RX_SLOTS 8u
#define FL_NET_WIFI_RX_MAX FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
    size_t len;
    uint8_t data[FL_NET_WIFI_RX_MAX];
} fl_net_wifi_rx_slot_t;

static fl_net_driver_t s_wifi_drv;
static fl_net_wifi_rx_slot_t s_wifi_rx[FL_NET_WIFI_RX_SLOTS];
static unsigned s_wifi_rx_head;
static unsigned s_wifi_rx_count;
static int s_wifi_up;
static uint8_t s_ap_bssid[6];
static uint8_t s_sta_mac[6];
static char s_joined_ssid[FL_WIFI_SSID_MAX];
static uint32_t s_wifi_ip_be;
static uint32_t s_wifi_netmask_be;
static uint32_t s_wifi_gw_be;
static uint32_t s_wifi_dns_be;
static uint8_t s_wifi_ip6[16];
static uint8_t s_wifi_prefix6;
static int s_wifi_has_ipv6;

static fl_result_t wifi_rx_enqueue(const uint8_t *frame, size_t len)
{
    unsigned idx;

    if (!frame || len == 0 || len > FL_NET_WIFI_RX_MAX)
        return FL_RESULT_INVAL;
    if (s_wifi_rx_count >= FL_NET_WIFI_RX_SLOTS)
        return FL_RESULT_ERR;
    idx = (s_wifi_rx_head + s_wifi_rx_count) % FL_NET_WIFI_RX_SLOTS;
    memcpy(s_wifi_rx[idx].data, frame, len);
    s_wifi_rx[idx].len = len;
    s_wifi_rx_count++;
    return FL_RESULT_OK;
}

static fl_result_t wifi_rx_dequeue(uint8_t *buf, size_t cap, size_t *len)
{
    if (!buf || !len)
        return FL_RESULT_INVAL;
    if (s_wifi_rx_count == 0)
        return FL_RESULT_TIMEDOUT;
    if (cap < s_wifi_rx[s_wifi_rx_head].len)
        return FL_RESULT_ERR;
    *len = s_wifi_rx[s_wifi_rx_head].len;
    memcpy(buf, s_wifi_rx[s_wifi_rx_head].data, *len);
    s_wifi_rx_head = (s_wifi_rx_head + 1u) % FL_NET_WIFI_RX_SLOTS;
    s_wifi_rx_count--;
    return FL_RESULT_OK;
}

static fl_result_t wifi_build_eth_ipv4_reply(const uint8_t *req_ip, size_t req_ip_len,
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

static fl_result_t wifi_try_udp_echo(const uint8_t *ip, size_t ip_len, uint8_t *ip_reply,
                                     size_t ip_reply_cap, size_t *ip_reply_len)
{
    size_t ihl;
    const uint8_t *udp;
    uint16_t sport;
    uint16_t dport;
    uint16_t udp_total;
    size_t payload_len;
    uint8_t udp_buf[FL_NET_UDP_HDR_LEN + FL_NET_UDP_LAB_RX_PAYLOAD_MAX];
    size_t udp_len;
    uint32_t src_be;
    uint32_t dst_be;

    if (!ip || ip_len < FL_NET_IPV4_HDR_LEN_MIN || !ip_reply || !ip_reply_len)
        return FL_RESULT_ERR;
    if (ip[9] != FL_NET_IP_PROTO_UDP)
        return FL_RESULT_ERR;

    ihl = (size_t)((ip[0] & 0x0fu) * 4u);
    if (ip_len < ihl + FL_NET_UDP_HDR_LEN)
        return FL_RESULT_ERR;
    udp = ip + ihl;
    sport = (uint16_t)(((uint16_t)udp[0] << 8) | (uint16_t)udp[1]);
    dport = (uint16_t)(((uint16_t)udp[2] << 8) | (uint16_t)udp[3]);
    udp_total = (uint16_t)(((uint16_t)udp[4] << 8) | (uint16_t)udp[5]);
    if (udp_total < FL_NET_UDP_HDR_LEN || ihl + udp_total > ip_len)
        return FL_RESULT_ERR;
    payload_len = (size_t)udp_total - FL_NET_UDP_HDR_LEN;

    src_be = (uint32_t)ip[12] | ((uint32_t)ip[13] << 8) | ((uint32_t)ip[14] << 16) |
             ((uint32_t)ip[15] << 24);
    dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) | ((uint32_t)ip[18] << 16) |
             ((uint32_t)ip[19] << 24);

    udp_len = fl_net_udp_build_datagram(udp_buf, sizeof(udp_buf), dst_be, src_be, dport, sport,
                                        udp + FL_NET_UDP_HDR_LEN, payload_len);
    if (udp_len == 0)
        return FL_RESULT_ERR;
    if (wifi_build_eth_ipv4_reply(ip, ip_len, udp_buf, udp_len, ip_reply, ip_reply_cap) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    *ip_reply_len = (size_t)(((ip_reply[2] & 0xffu) << 8) | (ip_reply[3] & 0xffu));
    return FL_RESULT_OK;
}

static uint32_t ipv4_prefix_to_mask_be(uint8_t prefix)
{
    if (prefix == 0u)
        return 0u;
    if (prefix >= 32u)
        return UINT32_C(0xffffffff);
    return htonl((uint32_t)(UINT32_C(0xffffffff) << (32u - prefix)));
}

static void wifi_lab_profile_from_env(uint32_t *yiaddr_be, uint8_t *prefix_len, uint32_t *gw_be,
                                      uint32_t *dns_be)
{
    const char *ip_s = getenv("FL_NET_WIFI_LAB_IPV4");
    const char *pfx_s = getenv("FL_NET_WIFI_LAB_PREFIX");
    const char *gw_s = getenv("FL_NET_WIFI_LAB_GW");
    const char *dns_s = getenv("FL_NET_WIFI_LAB_DNS");
    uint32_t ip = 0u;
    uint32_t gw = 0u;
    uint32_t dns = 0u;
    unsigned long pfx = 24u;

    if (!ip_s || !ip_s[0])
        ip_s = "10.0.2.15";
    if (!gw_s || !gw_s[0])
        gw_s = "10.0.2.2";
    if (!dns_s || !dns_s[0])
        dns_s = "10.0.2.3";
    if (pfx_s && pfx_s[0]) {
        char *end = NULL;
        unsigned long v = strtoul(pfx_s, &end, 10);
        if (end != pfx_s && v > 0u && v <= 32u)
            pfx = v;
    }
    (void)fl_net_ipv4_parse_literal(ip_s, &ip);
    (void)fl_net_ipv4_parse_literal(gw_s, &gw);
    (void)fl_net_ipv4_parse_literal(dns_s, &dns);
    if (yiaddr_be)
        *yiaddr_be = ip;
    if (prefix_len)
        *prefix_len = (uint8_t)pfx;
    if (gw_be)
        *gw_be = gw;
    if (dns_be)
        *dns_be = dns;
}

static void wifi_store_l3_profile(uint32_t ip_be, uint8_t prefix_len, uint32_t gw_be,
                                  uint32_t dns_be)
{
    if (gw_be == 0u)
        (void)fl_net_ipv4_parse_literal("10.0.2.2", &gw_be);
    s_wifi_ip_be = ip_be;
    s_wifi_netmask_be = ipv4_prefix_to_mask_be(prefix_len);
    s_wifi_gw_be = gw_be;
    s_wifi_dns_be = dns_be;
}

static fl_result_t wifi_try_arp_reply(const uint8_t *frame, size_t len, uint8_t *eth_reply,
                                      size_t cap, size_t *eth_len)
{
    uint16_t op;
    uint32_t sender_ip;
    uint32_t target_ip;
    uint8_t sender_mac[6];
    uint8_t target_mac[6];
    size_t reply_len;

    if (!frame || !eth_reply || !eth_len || s_wifi_ip_be == 0u)
        return FL_RESULT_ERR;
    if (!fl_net_arp_parse_eth(frame, len, &op, &sender_ip, sender_mac, &target_ip, target_mac))
        return FL_RESULT_ERR;
    if (op != 1u)
        return FL_RESULT_ERR;
    if (target_ip != s_wifi_ip_be && target_ip != s_wifi_gw_be)
        return FL_RESULT_ERR;
    reply_len = fl_net_arp_build_reply(eth_reply, cap, s_sta_mac,
                                       target_ip == s_wifi_gw_be ? s_wifi_gw_be : s_wifi_ip_be,
                                       sender_mac, sender_ip);
    if (reply_len == 0u)
        return FL_RESULT_ERR;
    *eth_len = reply_len;
    return FL_RESULT_OK;
}

static fl_result_t wifi_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame)
{
    size_t ip_off;
    size_t ip_len;
    uint32_t dst_be;
    uint8_t eth_reply[FL_NET_WIFI_RX_MAX];
    size_t eth_len = 0;

    (void)drv;
    if (!s_wifi_up || !frame)
        return FL_RESULT_INVAL;
    if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    {
        uint8_t arp_reply[FL_NET_WIFI_RX_MAX];
        size_t arp_len = 0;

        if (wifi_try_arp_reply(frame->data, frame->len, arp_reply, sizeof(arp_reply),
                               &arp_len) == FL_RESULT_OK) {
            (void)wifi_rx_enqueue(arp_reply, arp_len);
            return FL_RESULT_OK;
        }
    }
    if (!fl_net_wire_parse_eth_ipv4(frame->data, frame->len, &ip_off, &ip_len, &dst_be))
        return FL_RESULT_OK;
    {
        const uint8_t *ip = frame->data + ip_off;
        uint8_t ip_reply[FL_NET_WIFI_RX_MAX];
        size_t ip_reply_len = 0;

        if (ip_len < FL_NET_IPV4_HDR_LEN_MIN)
            return FL_RESULT_OK;
        if (ip[9] == FL_NET_IP_PROTO_ICMP && ip_len > (size_t)((ip[0] & 0x0fu) * 4u)) {
            const uint8_t *icmp = ip + (size_t)((ip[0] & 0x0fu) * 4u);
            size_t icmp_len = ip_len - (size_t)((ip[0] & 0x0fu) * 4u);
            if (icmp_len >= FL_NET_ICMPV4_HDR_MIN && icmp[0] == FL_NET_ICMPV4_TYPE_ECHO) {
                if (wifi_build_eth_ipv4_reply(ip, ip_len, icmp, icmp_len, ip_reply,
                                              sizeof(ip_reply)) != FL_RESULT_OK)
                    return FL_RESULT_OK;
                ip_reply_len = ip_len;
                ip_reply[(size_t)((ip_reply[0] & 0x0fu) * 4u)] =
                    (uint8_t)FL_NET_ICMPV4_TYPE_ECHO_REPLY;
            }
        } else if (ip[9] == FL_NET_IP_PROTO_UDP) {
            if (wifi_try_udp_echo(ip, ip_len, ip_reply, sizeof(ip_reply), &ip_reply_len) !=
                FL_RESULT_OK)
                ip_reply_len = 0u;
        }
        if (ip_reply_len > 0u) {
            eth_len = fl_net_wire_build_eth_ipv4(eth_reply, sizeof(eth_reply), s_sta_mac,
                                                 s_ap_bssid, ip_reply, ip_reply_len);
            if (eth_len > 0u)
                (void)wifi_rx_enqueue(eth_reply, eth_len);
        }
    }
    return FL_RESULT_OK;
}

static fl_result_t wifi_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out)
{
    fl_result_t rc;

    (void)drv;
    if (!out)
        return FL_RESULT_INVAL;
    rc = wifi_rx_dequeue(out->data, out->cap, &out->len);
    if (rc == FL_RESULT_OK)
        (void)fl_net_wire_check_rx_fill(out, out->len);
    return rc;
}

fl_net_driver_t *fl_net_wifi_netdev_driver(void)
{
    return s_wifi_up ? &s_wifi_drv : NULL;
}

int fl_net_wifi_netdev_is_up(void)
{
    return s_wifi_up;
}

const char *fl_net_wifi_netdev_iface(void)
{
    return FL_NET_WIFI_STATION_IFNAME;
}

static fl_result_t wifi_netdev_up_common(const fl_net_wifi_scan_entry_t *ap,
                                         const uint8_t sta_mac[6])
{
    if (!ap || !sta_mac)
        return FL_RESULT_INVAL;
    memset(&s_wifi_drv, 0, sizeof(s_wifi_drv));
    s_wifi_drv.send = wifi_driver_send;
    s_wifi_drv.recv = wifi_driver_recv;
    s_wifi_drv.mtu = FL_NET_ETH_MTU_DEFAULT;
    memcpy(s_ap_bssid, ap->bssid, 6);
    memcpy(s_sta_mac, sta_mac, 6);
    strncpy(s_joined_ssid, ap->ssid, sizeof(s_joined_ssid) - 1u);
    s_wifi_rx_head = 0u;
    s_wifi_rx_count = 0u;
    s_wifi_up = 1;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_netdev_up(const fl_net_wifi_scan_entry_t *ap, const uint8_t sta_mac[6])
{
    return wifi_netdev_up_common(ap, sta_mac);
}

fl_result_t fl_net_wifi_netdev_up_with_ipv4(const fl_net_wifi_scan_entry_t *ap,
                                            const uint8_t sta_mac[6], const char *addr_s,
                                            uint8_t prefix_len, const char *gw_s)
{
    fl_result_t rc;
    uint32_t ip_be = 0u;
    const char *gw = gw_s;
    uint32_t gw_be = 0u;
    uint32_t dns_be = 0u;

    rc = wifi_netdev_up_common(ap, sta_mac);
    if (rc != FL_RESULT_OK)
        return rc;
    if (!addr_s || !addr_s[0] || !fl_net_ipv4_parse_literal(addr_s, &ip_be))
        return FL_RESULT_ERR;
    if (!gw || !gw[0]) {
        static char gw_buf[16];
        uint32_t host = ntohl(ip_be);
        snprintf(gw_buf, sizeof(gw_buf), "%u.%u.%u.1",
                 (unsigned)((host >> 24) & 0xffu), (unsigned)((host >> 16) & 0xffu),
                 (unsigned)((host >> 8) & 0xffu));
        gw = gw_buf;
    }
    if (prefix_len == 0u)
        prefix_len = 24u;
    (void)fl_net_ipv4_parse_literal(gw, &gw_be);
    wifi_lab_profile_from_env(NULL, NULL, NULL, &dns_be);
    wifi_store_l3_profile(ip_be, prefix_len, gw_be, dns_be);
    rc = fl_net_route_configure_static(&s_wifi_drv, sta_mac, addr_s, prefix_len, gw);
    if (rc != FL_RESULT_OK) {
        fl_net_wifi_netdev_down();
        return rc;
    }
    fl_net_iface_refresh();
    return rc;
}

void fl_net_wifi_netdev_down(void)
{
    if (s_wifi_up)
        fl_net_route_remove_drv(&s_wifi_drv);
    s_wifi_up = 0;
    s_wifi_rx_head = 0u;
    s_wifi_rx_count = 0u;
    s_joined_ssid[0] = '\0';
    s_wifi_ip_be = 0u;
    s_wifi_netmask_be = 0u;
    s_wifi_gw_be = 0u;
    s_wifi_dns_be = 0u;
    memset(s_wifi_ip6, 0, sizeof(s_wifi_ip6));
    s_wifi_prefix6 = 0u;
    s_wifi_has_ipv6 = 0;
    fl_net_iface_refresh();
}

fl_result_t fl_net_wifi_netdev_ipv4(uint32_t *addr_be_out)
{
    if (!s_wifi_up)
        return FL_RESULT_NOENT;
    if (addr_be_out)
        *addr_be_out = s_wifi_ip_be;
    return FL_RESULT_OK;
}

void fl_net_wifi_netdev_apply_dhcp_lease(const fl_net_dhcp_lease_info_t *lease)
{
    char addr_s[32];
    char gw_s[32];
    uint8_t prefix = 24u;
    fl_result_t rc;

    if (!lease || lease->yiaddr_be == 0u || !s_wifi_up)
        return;
    if (lease->has_prefix)
        prefix = lease->prefix_len;
    fl_net_ipv4_format_addr(lease->yiaddr_be, addr_s, sizeof(addr_s));
    if (lease->has_gateway)
        fl_net_ipv4_format_addr(lease->gateway_be, gw_s, sizeof(gw_s));
    else if (s_wifi_gw_be != 0u)
        fl_net_ipv4_format_addr(s_wifi_gw_be, gw_s, sizeof(gw_s));
    else
        strncpy(gw_s, "10.0.2.2", sizeof(gw_s) - 1u);
    wifi_store_l3_profile(lease->yiaddr_be, prefix,
                          lease->has_gateway ? lease->gateway_be : s_wifi_gw_be,
                          lease->has_dns ? lease->dns_be : s_wifi_dns_be);
    rc = fl_net_route_configure_static(&s_wifi_drv, s_sta_mac, addr_s, prefix, gw_s);
    if (rc == FL_RESULT_OK)
        fl_net_iface_refresh();
}

int fl_net_wifi_netdev_l3_profile(fl_net_wifi_l3_profile_t *out)
{
    if (!out || !s_wifi_up || s_wifi_ip_be == 0u)
        return 0;
    out->ipv4 = s_wifi_ip_be;
    out->netmask = s_wifi_netmask_be;
    out->gateway = s_wifi_gw_be;
    out->dns = s_wifi_dns_be;
    return 1;
}

fl_result_t fl_net_wifi_netdev_add_ipv6(const uint8_t src6[16], uint8_t prefix_len)
{
    uint8_t net6[16];

    if (!s_wifi_up || !src6 || prefix_len == 0u || prefix_len > FL_NET_IPV6_MAX_PREFIX_LEN)
        return FL_RESULT_INVAL;
    fl_net_ipv6_network_addr(src6, prefix_len, net6);
    if (fl_net_route_add6(net6, prefix_len, &s_wifi_drv, src6, s_sta_mac) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    memcpy(s_wifi_ip6, src6, 16);
    s_wifi_prefix6 = prefix_len;
    s_wifi_has_ipv6 = 1;
    fl_net_iface_refresh();
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_netdev_ipv6(uint8_t addr6[16], uint8_t *prefix_len_out)
{
    if (!s_wifi_up || !s_wifi_has_ipv6)
        return FL_RESULT_NOENT;
    if (addr6)
        memcpy(addr6, s_wifi_ip6, 16);
    if (prefix_len_out)
        *prefix_len_out = s_wifi_prefix6;
    return FL_RESULT_OK;
}

int fl_net_wifi_netdev_peer_mac(uint8_t mac[6])
{
    if (!s_wifi_up || !mac)
        return 0;
    memcpy(mac, s_ap_bssid, 6);
    return 1;
}

int fl_net_wifi_netdev_sta_mac(uint8_t mac[6])
{
    if (!s_wifi_up || !mac)
        return 0;
    memcpy(mac, s_sta_mac, 6);
    return 1;
}
