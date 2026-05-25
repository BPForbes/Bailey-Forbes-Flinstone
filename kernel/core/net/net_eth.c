#include "net_eth.h"

#include "contract_p3_wire.h"

#include <string.h>

void fl_net_loopback_mac_host(uint8_t mac[6]) {
    if (!mac)
        return;
    mac[0] = FL_NET_LOOPBACK_MAC_HOST_0;
    mac[1] = FL_NET_LOOPBACK_MAC_HOST_1;
    mac[2] = FL_NET_LOOPBACK_MAC_HOST_2;
    mac[3] = FL_NET_LOOPBACK_MAC_HOST_3;
    mac[4] = FL_NET_LOOPBACK_MAC_HOST_4;
    mac[5] = FL_NET_LOOPBACK_MAC_HOST_5;
}

void fl_net_loopback_mac_peer(uint8_t mac[6]) {
    if (!mac)
        return;
    mac[0] = FL_NET_LOOPBACK_MAC_HOST_0;
    mac[1] = FL_NET_LOOPBACK_MAC_HOST_1;
    mac[2] = FL_NET_LOOPBACK_MAC_HOST_2;
    mac[3] = FL_NET_LOOPBACK_MAC_HOST_3;
    mac[4] = FL_NET_LOOPBACK_MAC_HOST_4;
    mac[5] = FL_NET_LOOPBACK_MAC_PEER_5;
}

size_t fl_net_eth_build_ipv4(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                             const uint8_t src_mac[6], const uint8_t *ipv4, size_t ipv4_len) {
    size_t total;

    if (!frame || !dst_mac || !src_mac || !ipv4 || ipv4_len == 0)
        return 0;
    total = FL_NET_ETH_HDR_LEN + ipv4_len;
    if (cap < total)
        return 0;

    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    frame[12] = (uint8_t)(FL_ETHERTYPE_IPV4 >> 8);
    frame[13] = (uint8_t)(FL_ETHERTYPE_IPV4 & 0xff);
    memcpy(frame + FL_NET_ETH_HDR_LEN, ipv4, ipv4_len);
    return total;
}

int fl_net_eth_parse_ipv4(const uint8_t *frame, size_t len, size_t *ip_off, size_t *ip_len,
                          uint32_t *dst_be) {
    uint16_t ethertype;
    size_t off;
    size_t iplen;
    const uint8_t *ip;

    if (!frame || len < FL_NET_ETH_HDR_LEN + FL_NET_IPV4_HDR_LEN_MIN || !ip_off || !ip_len ||
        !dst_be)
        return 0;

    ethertype = (uint16_t)(((uint16_t)frame[12] << 8) | frame[13]);
    if (ethertype != FL_ETHERTYPE_IPV4)
        return 0;

    off = FL_NET_ETH_HDR_LEN;
    ip = frame + off;
    iplen = (size_t)(((uint16_t)ip[2] << 8) | ip[3]);
    if (iplen < FL_NET_IPV4_HDR_LEN_MIN || off + iplen > len)
        return 0;

    *ip_off = off;
    *ip_len = iplen;
    *dst_be = (uint32_t)ip[16] | ((uint32_t)ip[17] << 8) | ((uint32_t)ip[18] << 16) |
              ((uint32_t)ip[19] << 24);
    return 1;
}
