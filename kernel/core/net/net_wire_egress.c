#include "net_wire_egress.h"

#include "contract_p3_ipv4.h"
#include "net_arp.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_wire.h"

#include <string.h>
#include <sys/time.h>

static double egress_delta_ms(const struct timeval *t0, const struct timeval *t1) {
    return (double)(t1->tv_sec - t0->tv_sec) * 1000.0 +
           (double)(t1->tv_usec - t0->tv_usec) / 1000.0;
}

static fl_result_t egress_loopback(uint32_t dst_be, uint8_t ip_proto, const uint8_t *l4,
                                   size_t l4_len, uint8_t *rx_l4, size_t rx_l4_cap,
                                   size_t *rx_l4_len, unsigned timeout_ms, double *out_rtt_ms) {
    uint8_t ipbuf[576];
    uint8_t frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t rx_frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    fl_net_ipv4_hdr_t hdr;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    size_t ip_len;
    size_t frame_len;
    size_t ip_off;
    size_t ip_len_rx;
    uint32_t dummy_dst;
    struct timeval t0, t1;
    fl_result_t rc;
    uint32_t src_be = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24);

    (void)ip_proto;

    fl_net_loopback_reset();

    ip_len = fl_net_ipv4_build(&hdr, ipbuf, sizeof(ipbuf), ip_proto, src_be, dst_be, l4, l4_len,
                               0x4242u);
    if (ip_len == 0)
        return FL_RESULT_ERR;

    fl_net_loopback_mac_peer(peer_mac);
    fl_net_loopback_mac_host(host_mac);
    frame_len = fl_net_wire_build_eth_ipv4(frame, sizeof(frame), peer_mac, host_mac, ipbuf, ip_len);
    if (frame_len == 0)
        return FL_RESULT_ERR;

    view.data = frame;
    view.len = frame_len;

    gettimeofday(&t0, NULL);
    rc = fl_net_netdev_send(fl_net_netdev_loopback(), &view);
    if (rc != FL_RESULT_OK)
        return rc;

    mut.data = rx_frame;
    mut.cap = sizeof(rx_frame);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, timeout_ms);
    gettimeofday(&t1, NULL);
    if (rc != FL_RESULT_OK)
        return rc;
    if (out_rtt_ms)
        *out_rtt_ms = egress_delta_ms(&t0, &t1);

    if (!fl_net_wire_parse_eth_ipv4(rx_frame, mut.len, &ip_off, &ip_len_rx, &dummy_dst))
        return FL_RESULT_ERR;
    {
        size_t hdr_len = (size_t)((rx_frame[ip_off] & 0x0fu) * 4u);
        size_t payload = ip_len_rx - hdr_len;
        if (payload > rx_l4_cap)
            return FL_RESULT_ERR;
        memcpy(rx_l4, rx_frame + ip_off + hdr_len, payload);
        *rx_l4_len = payload;
    }
    return FL_RESULT_OK;
}

static fl_result_t egress_process_rx_arp(const uint8_t *frame, size_t len,
                                         const fl_net_route_entry_t *route) {
    uint8_t reply[FL_NET_ETH_FRAME_HDR_LEN + FL_NET_ARP_PKT_LEN];
    size_t reply_len = 0;
    fl_result_t rc;

    if (!route || !route->src_mac_valid)
        return FL_RESULT_ERR;

    rc = fl_net_arp_input(frame, len, route->src_ip_be, route->src_mac, reply, sizeof(reply),
                          &reply_len);
    if (rc != FL_RESULT_OK || reply_len == 0)
        return FL_RESULT_ERR;

    {
        fl_net_frame_view_t view;
        view.data = reply;
        view.len = reply_len;
        return fl_net_netdev_send(route->drv, &view);
    }
}

static fl_result_t egress_tap_path(const fl_net_route_entry_t *route, uint32_t dst_be,
                                   uint8_t ip_proto, const uint8_t *l4, size_t l4_len,
                                   uint8_t *rx_l4, size_t rx_l4_cap, size_t *rx_l4_len,
                                   unsigned timeout_ms, double *out_rtt_ms) {
    uint8_t ipbuf[576];
    uint8_t tx_frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t rx_frame[FL_NET_WIRE_FRAME_BUF_MAX];
    uint8_t dst_mac[6];
    fl_net_ipv4_hdr_t hdr;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    size_t ip_len;
    size_t frame_len;
    uint32_t next_hop;
    struct timeval t0, t1;
    unsigned elapsed = 0;
    const unsigned step_ms = 50u;
    fl_result_t rc;

    if (!route->src_mac_valid)
        return FL_RESULT_ERR;

    next_hop = fl_net_route_next_hop(dst_be, route);

    rc = fl_net_arp_resolve(route->drv, route->src_mac, route->src_ip_be, next_hop, dst_mac,
                            timeout_ms);
    if (rc != FL_RESULT_OK)
        return rc;

    ip_len = fl_net_ipv4_build(&hdr, ipbuf, sizeof(ipbuf), ip_proto, route->src_ip_be, dst_be, l4,
                               l4_len, 0x5151u);
    if (ip_len == 0)
        return FL_RESULT_ERR;

    frame_len = fl_net_wire_build_eth_ipv4(tx_frame, sizeof(tx_frame), dst_mac, route->src_mac,
                                           ipbuf, ip_len);
    if (frame_len == 0)
        return FL_RESULT_ERR;

    view.data = tx_frame;
    view.len = frame_len;
    gettimeofday(&t0, NULL);
    if (fl_net_netdev_send(route->drv, &view) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    while (elapsed < timeout_ms) {
        unsigned wait = step_ms;
        size_t ip_off;
        size_t ip_len_rx;
        uint32_t rx_dst;
        uint16_t ethertype;
        int ok;

        if (timeout_ms - elapsed < wait)
            wait = timeout_ms - elapsed;

        mut.data = rx_frame;
        mut.cap = sizeof(rx_frame);
        mut.len = 0;
        rc = fl_net_netdev_recv(route->drv, &mut, wait);
        elapsed += wait;
        if (rc != FL_RESULT_OK)
            continue;

        ethertype = fl_net_wire_ethertype_be16(rx_frame, mut.len, &ok);
        if (ok && ethertype == FL_ETHERTYPE_ARP) {
            (void)egress_process_rx_arp(rx_frame, mut.len, route);
            continue;
        }

        if (!fl_net_wire_parse_eth_ipv4(rx_frame, mut.len, &ip_off, &ip_len_rx, &rx_dst))
            continue;
        if (rx_dst != route->src_ip_be)
            continue;

        {
            size_t hdr_len = (size_t)((rx_frame[ip_off] & 0x0fu) * 4u);
            size_t payload = ip_len_rx - hdr_len;
            if (payload > rx_l4_cap)
                return FL_RESULT_ERR;
            memcpy(rx_l4, rx_frame + ip_off + hdr_len, payload);
            *rx_l4_len = payload;
        }
        gettimeofday(&t1, NULL);
        if (out_rtt_ms)
            *out_rtt_ms = egress_delta_ms(&t0, &t1);
        return FL_RESULT_OK;
    }

    return FL_RESULT_TIMEDOUT;
}

fl_result_t fl_net_wire_egress_l4(uint32_t dst_be, uint8_t ip_proto, const uint8_t *l4,
                                  size_t l4_len, uint8_t *rx_l4, size_t rx_l4_cap,
                                  size_t *rx_l4_len, unsigned timeout_ms, double *out_rtt_ms) {
    fl_net_route_entry_t route;

    if (!l4 || l4_len == 0 || !rx_l4 || !rx_l4_len)
        return FL_RESULT_INVAL;

    if (fl_net_loopback_owns(dst_be))
        return egress_loopback(dst_be, ip_proto, l4, l4_len, rx_l4, rx_l4_cap, rx_l4_len,
                              timeout_ms, out_rtt_ms);

    if (fl_net_route_lookup(dst_be, &route) != FL_RESULT_OK)
        return FL_RESULT_NOENT;

    if (route.drv == fl_net_netdev_loopback())
        return egress_loopback(dst_be, ip_proto, l4, l4_len, rx_l4, rx_l4_cap, rx_l4_len,
                              timeout_ms, out_rtt_ms);

    if (route.drv == fl_net_netdev_tap() && fl_net_netdev_tap_is_open())
        return egress_tap_path(&route, dst_be, ip_proto, l4, l4_len, rx_l4, rx_l4_cap, rx_l4_len,
                               timeout_ms, out_rtt_ms);

    return FL_RESULT_NOENT;
}
