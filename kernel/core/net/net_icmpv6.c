#include "net_icmpv6.h"

#include "net_endian.h"
#include "net_ipv6.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_wire.h"

#include <string.h>
#include <sys/time.h>

size_t fl_net_icmpv6_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                        size_t payload_len) {
    size_t need;
    uint16_t csum;
    uint8_t lb[FL_NET_IPV6_ADDR_LEN];

    if (!buf || cap < FL_NET_ICMPV6_HDR_MIN)
        return 0;
    need = FL_NET_ICMPV6_HDR_MIN + payload_len;
    if (need > cap)
        return 0;

    buf[0] = (uint8_t)FL_NET_ICMPV6_TYPE_ECHO;
    buf[1] = 0;
    fl_net_put_u16_be(buf + 2, 0u);
    fl_net_put_u16_be(buf + 4, id);
    fl_net_put_u16_be(buf + 6, seq);
    if (payload_len > 0)
        memset(buf + FL_NET_ICMPV6_HDR_MIN, 0x5a, payload_len);

    fl_net_ipv6_loopback_addr(lb);
    csum = fl_net_ipv6_icmp6_checksum(lb, lb, buf, need);
    fl_net_put_u16_be(buf + 2, csum);
    return need;
}

int fl_net_icmpv6_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq) {
    if (!buf || len < FL_NET_ICMPV6_HDR_MIN)
        return 0;
    if (buf[0] != (uint8_t)FL_NET_ICMPV6_TYPE_ECHO_REPLY)
        return 0;
    if (fl_net_get_u16_be(buf + 4) != id)
        return 0;
    if (fl_net_get_u16_be(buf + 6) != seq)
        return 0;
    return 1;
}

fl_result_t fl_net_loopback_icmpv6_echo(const uint8_t *icmp_req, size_t icmp_len,
                                        const uint8_t *src6, const uint8_t *dst6,
                                        uint8_t *icmp_reply, size_t reply_cap,
                                        size_t *reply_len) {
    uint16_t csum;

    if (!icmp_req || icmp_len < FL_NET_ICMPV6_HDR_MIN || !icmp_reply || !reply_len || !src6 ||
        !dst6)
        return FL_RESULT_INVAL;
    if (reply_cap < icmp_len)
        return FL_RESULT_ERR;
    if (icmp_req[0] != (uint8_t)FL_NET_ICMPV6_TYPE_ECHO)
        return FL_RESULT_TIMEDOUT;

    memcpy(icmp_reply, icmp_req, icmp_len);
    icmp_reply[0] = (uint8_t)FL_NET_ICMPV6_TYPE_ECHO_REPLY;
    fl_net_put_u16_be(icmp_reply + 2, 0u);
    csum = fl_net_ipv6_icmp6_checksum(dst6, src6, icmp_reply, icmp_len);
    fl_net_put_u16_be(icmp_reply + 2, csum);
    *reply_len = icmp_len;
    return FL_RESULT_OK;
}

fl_result_t fl_net_icmpv6_echo_exchange(const uint8_t dst6[FL_NET_IPV6_ADDR_LEN], uint16_t id,
                                        uint16_t seq, unsigned timeout_ms, double *out_rtt_ms) {
    uint8_t icmp[FL_NET_ICMPV6_HDR_MIN + 32u];
    uint8_t ip6[128];
    uint8_t frame[256];
    uint8_t rx[256];
    uint8_t lb[FL_NET_IPV6_ADDR_LEN];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    size_t icmp_len;
    size_t ip6_len;
    size_t frame_len;
    fl_net_frame_mut_t mut;
    fl_net_frame_view_t view;
    struct timeval t0;
    struct timeval t1;
    fl_result_t rc;
    unsigned tries;
    unsigned max_tries;

    if (!dst6)
        return FL_RESULT_INVAL;
    if (!fl_net_ipv6_is_loopback(dst6))
        return FL_RESULT_NOSYS;

    fl_net_route_add6_loopback();
    fl_net_ipv6_loopback_addr(lb);
    icmp_len = fl_net_icmpv6_echo_request_build(icmp, sizeof(icmp), id, seq, 8u);
    if (icmp_len == 0u)
        return FL_RESULT_ERR;
    ip6_len = fl_net_ipv6_build(lb, lb, FL_NET_IP_PROTO_ICMPV6, icmp, icmp_len, ip6, sizeof(ip6));
    if (ip6_len == 0u)
        return FL_RESULT_ERR;
    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);
    frame_len = fl_net_wire_build_eth_ipv6(frame, sizeof(frame), peer_mac, host_mac, ip6, ip6_len);
    if (frame_len == 0u)
        return FL_RESULT_ERR;

    if (timeout_ms < 100u)
        timeout_ms = 100u;
    max_tries = (timeout_ms + 99u) / 100u;
    if (max_tries < 1u)
        max_tries = 1u;
    if (max_tries > 30u)
        max_tries = 30u;

    gettimeofday(&t0, NULL);
    view.data = frame;
    view.len = frame_len;
    rc = fl_net_netdev_send(fl_net_netdev_loopback(), &view);
    if (rc != FL_RESULT_OK)
        return rc;

    for (tries = 0; tries < max_tries; tries++) {
        size_t off = 0;
        size_t iplen = 0;

        mut.data = rx;
        mut.cap = sizeof(rx);
        mut.len = 0;
        rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 100u);
        if (rc != FL_RESULT_OK)
            continue;
        if (!fl_net_wire_parse_eth_ipv6(rx, mut.len, &off, &iplen))
            continue;
        if (iplen < FL_NET_IPV6_HDR_LEN + FL_NET_ICMPV6_HDR_MIN)
            continue;
        if (!fl_net_icmpv6_echo_reply_match(rx + off + FL_NET_IPV6_HDR_LEN,
                                            iplen - FL_NET_IPV6_HDR_LEN, id, seq))
            continue;
        gettimeofday(&t1, NULL);
        if (out_rtt_ms) {
            *out_rtt_ms =
                (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                (double)(t1.tv_usec - t0.tv_usec) / 1000.0;
        }
        return FL_RESULT_OK;
    }
    return FL_RESULT_TIMEDOUT;
}
