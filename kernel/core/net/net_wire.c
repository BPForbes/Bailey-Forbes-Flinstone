#include "net_wire.h"

#include "net_ipv6.h"

#include <string.h>

_Static_assert(FL_NET_WIRE_EXPECT_REV == FL_CONTRACT_P3_WIRE_REV,
               "net_wire.c must match contract_p3_wire.h FL_CONTRACT_P3_WIRE_REV");
_Static_assert(FL_NET_ETH_FRAME_HDR_LEN == 14u, "DIX Ethernet header is 14 octets");
_Static_assert(FL_NET_WIRE_FRAME_BUF_MAX >= FL_NET_ETH_FRAME_HDR_LEN + FL_NET_IPV4_HDR_LEN_MIN,
               "wire frame buffer fits minimum IPv4 over Ethernet");

void fl_net_wire_init(void) {
    /* Contract vocabulary lock; no runtime state today. */
}

fl_net_frame_view_t fl_net_frame_view_make(const uint8_t *data, size_t len) {
    fl_net_frame_view_t view;
    view.data = data;
    view.len = len;
    return view;
}

fl_net_frame_mut_t fl_net_frame_mut_make(uint8_t *data, size_t cap) {
    fl_net_frame_mut_t mut;
    mut.data = data;
    mut.cap = cap;
    mut.len = 0;
    return mut;
}

fl_net_frame_view_t fl_net_frame_view_from_mut(const fl_net_frame_mut_t *mut) {
    fl_net_frame_view_t view;
    if (!mut) {
        view.data = NULL;
        view.len = 0;
        return view;
    }
    view.data = mut->data;
    view.len = mut->len;
    return view;
}

fl_result_t fl_net_wire_check_view(const fl_net_frame_view_t *view, size_t min_len) {
    if (!view || !view->data)
        return FL_RESULT_INVAL;
    if (view->len < min_len)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wire_check_mut(const fl_net_frame_mut_t *mut) {
    if (!mut || !mut->data || mut->cap == 0)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

size_t fl_net_wire_frame_max(unsigned mtu) {
    unsigned m = mtu ? mtu : (unsigned)FL_NET_ETH_MTU_DEFAULT;
    if (m > (unsigned)FL_NET_ETH_MTU_DEFAULT)
        m = (unsigned)FL_NET_ETH_MTU_DEFAULT;
    return (size_t)FL_NET_ETH_FRAME_HDR_LEN + (size_t)m;
}

fl_result_t fl_net_wire_check_tx(const fl_net_frame_view_t *frame, unsigned mtu) {
    size_t max;

    if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    max = fl_net_wire_frame_max(mtu);
    if (frame->len > max)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

fl_result_t fl_net_wire_check_rx_fill(fl_net_frame_mut_t *out, size_t received) {
    if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (received > out->cap)
        return FL_RESULT_INVAL;
    out->len = received;
    return FL_RESULT_OK;
}

uint16_t fl_net_wire_ethertype_be16(const uint8_t *frame, size_t len, int *ok) {
    if (ok)
        *ok = 0;
    if (!frame || len < FL_NET_ETH_FRAME_HDR_LEN)
        return 0;
    if (ok)
        *ok = 1;
    return (uint16_t)(((uint16_t)frame[12] << 8) | (uint16_t)frame[13]);
}

int fl_net_wire_ethertype_is_ipv4(const uint8_t *frame, size_t len) {
    int ok = 0;
    uint16_t ethertype = fl_net_wire_ethertype_be16(frame, len, &ok);
    return ok && ethertype == FL_ETHERTYPE_IPV4;
}

void fl_net_wire_mac_copy(fl_eth_mac_t dst, const uint8_t src[FL_NET_ETH_ADDR_LEN]) {
    if (!dst || !src)
        return;
    memcpy(dst, src, FL_NET_ETH_ADDR_LEN);
}

int fl_net_wire_mac_equal(const uint8_t a[FL_NET_ETH_ADDR_LEN],
                          const uint8_t b[FL_NET_ETH_ADDR_LEN]) {
    if (!a || !b)
        return 0;
    return memcmp(a, b, FL_NET_ETH_ADDR_LEN) == 0;
}

size_t fl_net_wire_build_eth_ipv4(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                                  const uint8_t src_mac[6], const uint8_t *ipv4,
                                  size_t ipv4_len) {
    size_t total;
    fl_net_frame_view_t probe;

    if (!frame || !dst_mac || !src_mac || !ipv4 || ipv4_len == 0)
        return 0;
    if (ipv4_len > (size_t)FL_NET_ETH_MTU_DEFAULT)
        return 0;

    total = (size_t)FL_NET_ETH_FRAME_HDR_LEN + ipv4_len;
    if (cap < total)
        return 0;

    memcpy(frame, dst_mac, FL_NET_ETH_ADDR_LEN);
    memcpy(frame + FL_NET_ETH_ADDR_LEN, src_mac, FL_NET_ETH_ADDR_LEN);
    frame[12] = (uint8_t)(FL_ETHERTYPE_IPV4 >> 8);
    frame[13] = (uint8_t)(FL_ETHERTYPE_IPV4 & 0xff);
    memcpy(frame + FL_NET_ETH_FRAME_HDR_LEN, ipv4, ipv4_len);

    probe = fl_net_frame_view_make(frame, total);
    if (fl_net_wire_check_tx(&probe, FL_NET_ETH_MTU_DEFAULT) != FL_RESULT_OK)
        return 0;

    return total;
}

size_t fl_net_wire_build_eth_ipv6(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                                  const uint8_t src_mac[6], const uint8_t *ipv6,
                                  size_t ipv6_len) {
    size_t total;
    fl_net_frame_view_t probe;

    if (!frame || !dst_mac || !src_mac || !ipv6 || ipv6_len < FL_NET_IPV6_HDR_LEN)
        return 0;
    if (ipv6_len > (size_t)FL_NET_ETH_MTU_DEFAULT)
        return 0;

    total = (size_t)FL_NET_ETH_FRAME_HDR_LEN + ipv6_len;
    if (cap < total)
        return 0;

    memcpy(frame, dst_mac, FL_NET_ETH_ADDR_LEN);
    memcpy(frame + FL_NET_ETH_ADDR_LEN, src_mac, FL_NET_ETH_ADDR_LEN);
    frame[12] = (uint8_t)(FL_ETHERTYPE_IPV6 >> 8);
    frame[13] = (uint8_t)(FL_ETHERTYPE_IPV6 & 0xff);
    memcpy(frame + FL_NET_ETH_FRAME_HDR_LEN, ipv6, ipv6_len);

    probe = fl_net_frame_view_make(frame, total);
    if (fl_net_wire_check_tx(&probe, FL_NET_ETH_MTU_DEFAULT) != FL_RESULT_OK)
        return 0;
    return total;
}

int fl_net_wire_ethertype_is_ipv6(const uint8_t *frame, size_t len) {
    int ok = 0;
    uint16_t et = fl_net_wire_ethertype_be16(frame, len, &ok);
    return ok && et == FL_ETHERTYPE_IPV6;
}

int fl_net_wire_parse_eth_ipv6(const uint8_t *frame, size_t len, size_t *ip_off,
                               size_t *ip_len) {
    size_t off;
    const uint8_t *ip;

    if (!frame || len < FL_NET_ETH_FRAME_HDR_LEN + FL_NET_IPV6_HDR_LEN || !ip_off || !ip_len)
        return 0;
    if (!fl_net_wire_ethertype_is_ipv6(frame, len))
        return 0;

    off = (size_t)FL_NET_ETH_FRAME_HDR_LEN;
    ip = frame + off;
    {
        size_t pl_off = 0;
        size_t pl_len = 0;
        if (!fl_net_ipv6_parse(ip, len - off, &pl_off, &pl_len, NULL))
            return 0;
        *ip_off = off;
        *ip_len = pl_off + pl_len;
    }
    return 1;
}

int fl_net_wire_parse_eth_ipv4(const uint8_t *frame, size_t len, size_t *ip_off,
                               size_t *ip_len, fl_ipv4_be32_t *dst_addr) {
    size_t off;
    size_t iplen;
    const uint8_t *ip;

    if (!frame || len < FL_NET_ETH_FRAME_HDR_LEN + FL_NET_IPV4_HDR_LEN_MIN || !ip_off ||
        !ip_len || !dst_addr)
        return 0;

    if (!fl_net_wire_ethertype_is_ipv4(frame, len))
        return 0;

    off = (size_t)FL_NET_ETH_FRAME_HDR_LEN;
    ip = frame + off;
    iplen = (size_t)(((uint16_t)ip[2] << 8) | (uint16_t)ip[3]);
    if (iplen < FL_NET_IPV4_HDR_LEN_MIN || off + iplen > len)
        return 0;
    {
        uint8_t ver_ihl = ip[0];
        size_t ihl = (size_t)(ver_ihl & 0x0fu) * 4u;
        if ((ver_ihl >> 4) != 4u || ihl < FL_NET_IPV4_HDR_LEN_MIN || ihl > iplen)
            return 0;
    }

    *ip_off = off;
    *ip_len = iplen;
    *dst_addr = (fl_ipv4_be32_t)((uint32_t)ip[16] | ((uint32_t)ip[17] << 8) |
                                ((uint32_t)ip[18] << 16) | ((uint32_t)ip[19] << 24));
    return 1;
}

fl_net_frame_view_t fl_net_wire_slice_ipv4_payload(const uint8_t *frame, size_t len,
                                                   size_t *ip_off_out) {
    size_t ip_off = 0;
    size_t ip_len = 0;
    fl_ipv4_be32_t dst;
    fl_net_frame_view_t slice;

    slice.data = NULL;
    slice.len = 0;

    if (!fl_net_wire_parse_eth_ipv4(frame, len, &ip_off, &ip_len, &dst)) {
        if (ip_off_out)
            *ip_off_out = 0;
        return slice;
    }

    if (ip_off_out)
        *ip_off_out = ip_off;

    {
        size_t hdr_len = (size_t)((frame[ip_off] & 0x0fu) * 4u);
        if (ip_len <= hdr_len)
            return slice;
        slice.data = frame + ip_off + hdr_len;
        slice.len = ip_len - hdr_len;
    }
    return slice;
}

void fl_net_loopback_mac_host(uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    if (!mac)
        return;
    mac[0] = 0x02u;
    mac[1] = 0x00u;
    mac[2] = 0x5eu;
    mac[3] = 0x00u;
    mac[4] = 0x00u;
    mac[5] = 0x01u;
}

void fl_net_loopback_mac_peer(uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    if (!mac)
        return;
    fl_net_loopback_mac_host(mac);
    mac[5] = 0x02u;
}
