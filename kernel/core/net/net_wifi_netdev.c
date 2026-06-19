#include "net_wifi_netdev.h"

#include "contract_p3_ipv4.h"
#include "net_endian.h"
#include "net_iface.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_wire.h"
#include "net_checksum.h"
#include "net_ipv6.h"

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

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
static uint8_t s_wifi_ip6[16];
static uint8_t s_wifi_prefix6;
static int s_wifi_has_ipv6;

static fl_result_t wifi_rx_enqueue(const uint8_t *frame, size_t len) {
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

static fl_result_t wifi_rx_dequeue(uint8_t *buf, size_t cap, size_t *len) {
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
                                             uint8_t *out_ip, size_t out_cap) {
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
    out_ip[8] = req_ip[12];
    out_ip[9] = req_ip[13];
    out_ip[10] = req_ip[14];
    out_ip[11] = req_ip[15];
    out_ip[12] = req_ip[8];
    out_ip[13] = req_ip[9];
    out_ip[14] = req_ip[10];
    out_ip[15] = req_ip[11];
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

static fl_result_t wifi_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
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

static fl_result_t wifi_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    fl_result_t rc;
    (void)drv;
    if (!out)
        return FL_RESULT_INVAL;
    rc = wifi_rx_dequeue(out->data, out->cap, &out->len);
    if (rc == FL_RESULT_OK)
        (void)fl_net_wire_check_rx_fill(out, out->len);
    return rc;
}

fl_net_driver_t *fl_net_wifi_netdev_driver(void) {
    return s_wifi_up ? &s_wifi_drv : NULL;
}

int fl_net_wifi_netdev_is_up(void) {
    return s_wifi_up;
}

static fl_result_t wifi_netdev_up_common(const fl_net_wifi_scan_entry_t *ap,
                                           const uint8_t sta_mac[6]) {
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

fl_result_t fl_net_wifi_netdev_up(const fl_net_wifi_scan_entry_t *ap,
                                  const uint8_t sta_mac[6]) {
    fl_result_t rc = wifi_netdev_up_common(ap, sta_mac);
    if (rc != FL_RESULT_OK)
        return rc;
    if (!fl_net_ipv4_parse_literal("10.0.2.15", &s_wifi_ip_be))
        return FL_RESULT_ERR;
    (void)fl_net_route_configure_static(&s_wifi_drv, sta_mac, "10.0.2.15", 24u, "10.0.2.2");
    fl_net_iface_refresh();
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_netdev_up_with_ipv4(const fl_net_wifi_scan_entry_t *ap,
                                            const uint8_t sta_mac[6],
                                            const char *addr_s, uint8_t prefix_len,
                                            const char *gw_s) {
    fl_result_t rc;
    uint32_t ip_be = 0u;
    const char *gw = gw_s;

    rc = wifi_netdev_up_common(ap, sta_mac);
    if (rc != FL_RESULT_OK)
        return rc;
    if (!addr_s || !addr_s[0] || !fl_net_ipv4_parse_literal(addr_s, &ip_be))
        return FL_RESULT_ERR;
    s_wifi_ip_be = ip_be;
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
    rc = fl_net_route_configure_static(&s_wifi_drv, sta_mac, addr_s, prefix_len, gw);
    fl_net_iface_refresh();
    return rc;
}

void fl_net_wifi_netdev_down(void) {
    if (s_wifi_up)
        fl_net_route_remove_drv(&s_wifi_drv);
    s_wifi_up = 0;
    s_wifi_rx_head = 0u;
    s_wifi_rx_count = 0u;
    s_joined_ssid[0] = '\0';
    s_wifi_ip_be = 0u;
    memset(s_wifi_ip6, 0, sizeof(s_wifi_ip6));
    s_wifi_prefix6 = 0u;
    s_wifi_has_ipv6 = 0;
    fl_net_iface_refresh();
}

fl_result_t fl_net_wifi_netdev_ipv4(uint32_t *addr_be_out) {
    if (!s_wifi_up)
        return FL_RESULT_NOENT;
    if (addr_be_out)
        *addr_be_out = s_wifi_ip_be;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wifi_netdev_add_ipv6(const uint8_t src6[16], uint8_t prefix_len) {
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

fl_result_t fl_net_wifi_netdev_ipv6(uint8_t addr6[16], uint8_t *prefix_len_out) {
    if (!s_wifi_up || !s_wifi_has_ipv6)
        return FL_RESULT_NOENT;
    if (addr6)
        memcpy(addr6, s_wifi_ip6, 16);
    if (prefix_len_out)
        *prefix_len_out = s_wifi_prefix6;
    return FL_RESULT_OK;
}
