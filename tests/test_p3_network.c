#include "contract_p0_ci.h"
#include "contract_p3_ipv4.h"
#include "contract_p3_loopback.h"
#include "contract_p3_wire.h"
#include "net_arp.h"
#include "net_dns.h"
#include "net_eth.h"
#include "net_route.h"
#include "net_wire.h"
#include "net_checksum.h"
#include "net_icmp.h"
#include "net_icmpv6.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_ndp.h"
#include "net_dns.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_packet.h"
#include "net_ping_host.h"
#include "net_requirements.h"
#include "net_tap.h"
#include "net_wire_host.h"
#include "net_background.h"
#include "net_udp.h"
#include "net_socket.h"
#include "net_sock_native.h"
#include "net_dhcp.h"
#include "net_tcp.h"
#include "net_tcp_fsm.h"
#include "net_tls_hosted.h"
#include "net_http.h"
#include "net_tftp.h"
#include "net_client.h"
#include "net_server.h"
#include "net_wifi_station.h"
#include "net_wifi_netdev.h"
#include "contract_p3_dhcp.h"
#include "contract_p3_tls_hosted.h"
#include "contract_p3_socket.h"

#include "net_endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) \
    do { \
        if (!(c)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            return 1; \
        } \
    } while (0)

static int test_wire_vocabulary(void) {
    uint8_t buf[128];
    uint8_t ip[40];
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    size_t frame_len;
    size_t parsed_ip_len = 0;
    size_t ip_off = 0;
    fl_ipv4_be32_t dst = 0;

    ASSERT(FL_NET_WIRE_IMPL_DEFINED == 1);
    ASSERT(FL_CONTRACT_P3_WIRE_REV >= 3u);
    ASSERT(FL_NET_ETH_FRAME_HDR_LEN == 14u);
    ASSERT(fl_net_wire_frame_max(FL_NET_ETH_MTU_DEFAULT) == FL_NET_WIRE_FRAME_BUF_MAX);

    fl_net_wire_init();

    memset(ip, 0, sizeof(ip));
    ip[0] = 0x45;
    ip[9] = FL_NET_IP_PROTO_ICMP;
    ip[12] = 127;
    ip[16] = 127;
    ip[19] = 1;
    ip[2] = 0;
    ip[3] = 40;

    {
        uint8_t mac[6];
        fl_net_loopback_mac_host(mac);
        frame_len = fl_net_wire_build_eth_ipv4(buf, sizeof(buf), mac, mac, ip, 40u);
    }
    ASSERT(frame_len > FL_NET_ETH_FRAME_HDR_LEN);

    view = fl_net_frame_view_make(buf, frame_len);
    ASSERT(fl_net_wire_check_tx(&view, FL_NET_ETH_MTU_DEFAULT) == FL_RESULT_OK);
    ASSERT(fl_net_wire_ethertype_is_ipv4(buf, frame_len));
    ASSERT(fl_net_wire_parse_eth_ipv4(buf, frame_len, &ip_off, &parsed_ip_len, &dst));
    ASSERT(fl_net_ipv4_is_loopback((uint32_t)dst));

    mut = fl_net_frame_mut_make(buf, sizeof(buf));
    ASSERT(fl_net_wire_check_mut(&mut) == FL_RESULT_OK);
    ASSERT(fl_net_wire_check_rx_fill(&mut, 14u) == FL_RESULT_OK);
    ASSERT(mut.len == 14u);

    return 0;
}

static int test_icmp_echo_asm_layout(void) {
    uint8_t req[64];
    uint8_t reply[16];
    size_t len;
    len = fl_net_icmp_echo_request_build(req, sizeof(req), 0x1234u, 0x0009u, 4u);
    ASSERT(len == FL_NET_ICMPV4_HDR_MIN + 4u);
    ASSERT(req[0] == (uint8_t)FL_NET_ICMPV4_TYPE_ECHO);
    ASSERT(req[1] == 0);
    ASSERT(req[4] == 0x12 && req[5] == 0x34);
    ASSERT(req[6] == 0 && req[7] == 9);
    ASSERT(req[8] == 0x5a && req[9] == 0x5a);
    /* Ones-complement over the full echo (checksum field included) folds to 0. */
    ASSERT(fl_net_checksum16_valid(req, len));

    reply[0] = (uint8_t)FL_NET_ICMPV4_TYPE_ECHO_REPLY;
    reply[1] = 0;
    reply[2] = 0;
    reply[3] = 0;
    reply[4] = 0x12;
    reply[5] = 0x34;
    reply[6] = 0;
    reply[7] = 9;
    ASSERT(fl_net_icmp_echo_reply_match(reply, FL_NET_ICMPV4_HDR_MIN, 0x1234u, 9u));
    reply[0] = (uint8_t)FL_NET_ICMPV4_TYPE_ECHO;
    ASSERT(!fl_net_icmp_echo_reply_match(reply, FL_NET_ICMPV4_HDR_MIN, 0x1234u, 9u));
    return 0;
}

static int test_arp_cache_and_frame(void) {
    uint8_t mac[6] = {0x02, 0, 0x5e, 0, 0, 0x09};
    uint8_t out[6];
    uint8_t frame[64];
    uint8_t host_mac[6];
    uint16_t op = 0;
    size_t len;

    fl_net_arp_init();
    ASSERT(fl_net_arp_cache_insert((uint32_t)(10 | (9 << 8) | (8 << 16) | (7 << 24)), mac) ==
           FL_RESULT_OK);
    ASSERT(fl_net_arp_cache_lookup((uint32_t)(10 | (9 << 8) | (8 << 16) | (7 << 24)), out));
    ASSERT(memcmp(out, mac, 6) == 0);

    fl_net_loopback_mac_host(host_mac);
    len = fl_net_arp_build_request(frame, sizeof(frame), host_mac,
                                   fl_net_htonl(0x7F000001u),
                                   fl_net_htonl(0x7F000002u));
    ASSERT(len > FL_NET_ETH_FRAME_HDR_LEN);
    ASSERT(fl_net_arp_parse_eth(frame, len, &op, NULL, NULL, NULL, NULL));
    ASSERT(op == FL_NET_ARP_OP_REQUEST);
    return 0;
}

static int test_ipv4_prefix_match(void) {
    uint32_t a = (uint32_t)10 | (0 << 8) | (2 << 16) | (15 << 24);
    uint32_t n = (uint32_t)10 | (0 << 8) | (2 << 16) | (0 << 24);
    ASSERT(fl_net_ipv4_prefix_match(a, n, 24u));
    ASSERT(!fl_net_ipv4_prefix_match(a, n, 32u));
    ASSERT(fl_net_ipv4_network_addr(a, 24u) == n);
    ASSERT(fl_net_ipv4_prefix_match(a, fl_net_ipv4_network_addr(a, 24u), 24u));
    return 0;
}

static int test_loopback_arp_exchange(void) {
    uint8_t host_mac[6];
    uint8_t frame[128];
    uint8_t rx[128];
    size_t len;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_result_t rc;

    fl_net_arp_init();
    fl_net_route_init();
    fl_net_loopback_reset();
    fl_net_loopback_mac_host(host_mac);

    len = fl_net_arp_build_request(frame, sizeof(frame), host_mac,
                                   fl_net_htonl(0x7F000002u),
                                   fl_net_htonl(0x7F000001u));
    ASSERT(len > 0);
    view.data = frame;
    view.len = len;
    ASSERT(fl_net_netdev_send(fl_net_netdev_loopback(), &view) == FL_RESULT_OK);

    mut.data = rx;
    mut.cap = sizeof(rx);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(mut.len > FL_NET_ETH_FRAME_HDR_LEN);
    {
        uint16_t op = 0;
        ASSERT(fl_net_arp_parse_eth(rx, mut.len, &op, NULL, NULL, NULL, NULL));
        ASSERT(op == FL_NET_ARP_OP_REPLY);
    }
    return 0;
}

static int test_loopback_octet(void) {
    uint32_t a = 0u;
    ASSERT(fl_net_ipv4_parse_literal("127.0.0.1", &a) != 0);
    ASSERT(fl_net_ipv4_is_loopback(a));
    ASSERT(fl_net_ipv4_parse_literal("8.8.8.8", &a) != 0);
    ASSERT(!fl_net_ipv4_is_loopback(a));
    return 0;
}

static int test_resolve_localhost(void) {
    uint32_t addr_be = 0;
    /* 16 = "255.255.255.255\0" — same width as the libc INET_ADDRSTRLEN
     * literal we used to depend on, but no header is required. */
    char resolved[16];
    ASSERT(fl_net_resolve_ipv4("localhost", &addr_be, resolved, sizeof(resolved)) ==
           FL_RESULT_OK);
    ASSERT(fl_net_ipv4_is_loopback(addr_be));
    return 0;
}

static int test_icmp_unreachable_no_linux_fallback(void) {
    uint8_t req[FL_NET_ICMPV4_HDR_MIN + 8];
    uint8_t rx[64];
    size_t req_len;
    size_t rx_len = 0;
    uint32_t dst_be = 0u;
    fl_net_packet_t req_pkt;
    fl_net_packet_t rx_pkt;
    fl_result_t rc;

    ASSERT(fl_net_ipv4_parse_literal("203.0.113.99", &dst_be) != 0);

    req_len = fl_net_icmp_echo_request_build(req, sizeof(req), 0xabcdu, 1u, 8u);
    ASSERT(req_len > 0);
    ASSERT(fl_net_packet_bind_l4(&req_pkt, req, sizeof(req), 0u, req_len) == FL_RESULT_OK);

    rc = fl_net_wire_send_icmp_pkt(dst_be, &req_pkt, &rx_pkt, rx, sizeof(rx), &rx_len, 500u, NULL);
    ASSERT(rc == FL_RESULT_NOENT);
    return 0;
}

static int test_udp_unreachable_no_linux_fallback(void) {
    uint8_t payload[] = "dns-probe";
    uint8_t rx[64];
    size_t rx_len = 0;
    uint32_t dst_be = 0u;
    fl_net_packet_t tx_pkt;
    fl_net_packet_t rx_pkt;
    fl_result_t rc;

    ASSERT(fl_net_ipv4_parse_literal("203.0.113.99", &dst_be) != 0);

    ASSERT(fl_net_packet_bind_l4(&tx_pkt, payload, sizeof(payload) - 1u, 0u,
                               sizeof(payload) - 1u) == FL_RESULT_OK);

    rc = fl_net_wire_send_udp_pkt(dst_be, 40053u, 53u, &tx_pkt, &rx_pkt, rx, sizeof(rx), &rx_len,
                                  500u);
    ASSERT(rc == FL_RESULT_NOENT);
    return 0;
}


static int test_udp_echo_loopback(void) {
    const char payload[] = "udp-echo-roundtrip";
    uint8_t rx[64];
    size_t rx_len = 0;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    fl_result_t rc;

    fl_net_udp_demux_reset();
    ASSERT(fl_net_udp_bind_port(48002u) == FL_RESULT_OK);
    rc = fl_net_udp_echo_exchange(loopback, 48002u, 47002u, (const uint8_t *)payload,
                                  sizeof(payload) - 1u, rx, sizeof(rx), &rx_len, 3000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rx_len == sizeof(payload) - 1u);
    ASSERT(memcmp(rx, payload, rx_len) == 0);
    return 0;
}


static int test_tcp_connect_rst_slot_release(void) {
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    unsigned client_id = 0;
    fl_result_t rc;
    unsigned i;

    fl_net_tcp_fsm_reset();
    ASSERT(fl_net_tcp_listen_port(48820u) == FL_RESULT_OK);

    for (i = 0; i < 10u; i++) {
        rc = fl_net_tcp_connect(loopback, 48821u, &client_id);
        ASSERT(rc == FL_RESULT_EOF || rc == FL_RESULT_ERR || rc == FL_RESULT_TIMEDOUT);
    }

    rc = fl_net_tcp_connect(loopback, 48820u, &client_id);
    ASSERT(rc == FL_RESULT_OK);
    (void)fl_net_tcp_close(client_id);
    return 0;
}

static int test_tcp_dual_connect_loopback(void) {
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    unsigned c1 = 0;
    unsigned c2 = 0;
    unsigned s1 = 0;
    unsigned s2 = 0;
    const char msg[] = "dual";
    char rx[16];
    size_t rx_len = 0;
    fl_result_t rc;

    fl_net_tcp_fsm_reset();
    ASSERT(fl_net_tcp_listen_port(48830u) == FL_RESULT_OK);

    rc = fl_net_tcp_connect(loopback, 48830u, &c1);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_connect(loopback, 48830u, &c2);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_tcp_accept(48830u, &s1);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_accept(48830u, &s2);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(s1 != s2);

    rc = fl_net_tcp_send(c1, (const uint8_t *)msg, sizeof(msg) - 1u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_recv(s1, (uint8_t *)rx, sizeof(rx), &rx_len);
    ASSERT(rc == FL_RESULT_OK);

    (void)fl_net_tcp_close(c1);
    (void)fl_net_tcp_close(c2);
    (void)fl_net_tcp_close(s1);
    (void)fl_net_tcp_close(s2);
    return 0;
}

static int test_tcp_fsm_loopback(void) {
    unsigned server_id = 0;
    unsigned client_id = 0;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const char msg[] = "fsm-data";
    char rx[32];
    size_t rx_len = 0;
    fl_result_t rc;

    fl_net_tcp_fsm_reset();
    fl_net_udp_demux_reset();
    ASSERT(fl_net_tcp_listen_port(48790u) == FL_RESULT_OK);
    rc = fl_net_tcp_connect(loopback, 48790u, &client_id);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_accept(48790u, &server_id);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_send(client_id, (const uint8_t *)msg, sizeof(msg) - 1u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_recv(server_id, (uint8_t *)rx, sizeof(rx), &rx_len);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rx_len == sizeof(msg) - 1u);
    ASSERT(memcmp(rx, msg, rx_len) == 0);
    (void)fl_net_tcp_close(client_id);
    (void)fl_net_tcp_close(server_id);
    return 0;
}

static int test_loopback_ping(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping("127.0.0.1", 0, 1u, 3000u, &rtt, NULL, 0);
    if (rc == FL_RESULT_NOSYS || rc == FL_RESULT_TIMEDOUT) {
        fprintf(stderr, "skip: ICMP echo unavailable in this environment\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_loopback_tcp(void) {
    double rtt = 0.0;
    fl_result_t rc = fl_net_ping("127.0.0.1", 9, 1u, 3000u, &rtt, NULL, 0);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(rtt >= 0.0);
    return 0;
}

static int test_netdev_loopback_frame(void) {
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    uint8_t icmp[64];
    uint8_t ip[128];
    uint8_t frame[192];
    uint8_t rx[256];
    size_t icmp_len;
    size_t ip_len;
    size_t frame_len;
    uint32_t dst_be = fl_net_htonl(0x7F000001u);
    uint32_t src_be = fl_net_htonl(0x7F000001u);
    fl_net_ipv4_hdr_t hdr;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_net_netdev_stats_t stats;
    fl_result_t rc;

    fl_net_loopback_reset();

    icmp_len = fl_net_icmp_echo_request_build(icmp, sizeof(icmp), 0x4242u, 1u, 8u);
    ASSERT(icmp_len > 0);
    ip_len = fl_net_ipv4_build(&hdr, ip, sizeof(ip), FL_NET_IP_PROTO_ICMP, src_be, dst_be, icmp,
                               icmp_len, 0x1001u);
    ASSERT(ip_len > 0);

    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);
    frame_len = fl_net_eth_build_ipv4(frame, sizeof(frame), peer_mac, host_mac, ip, ip_len);
    ASSERT(frame_len > 0);

    view.data = frame;
    view.len = frame_len;
    ASSERT(fl_net_netdev_send(fl_net_netdev_loopback(), &view) == FL_RESULT_OK);

    mut.data = rx;
    mut.cap = sizeof(rx);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(mut.len > FL_NET_ETH_HDR_LEN);

    fl_net_netdev_stats(fl_net_netdev_loopback(), &stats);
    ASSERT(stats.tx_frames >= 1u);
    ASSERT(stats.rx_frames >= 1u);
    return 0;
}

static int test_ipv6_icmp_echo_loopback(void) {
    uint8_t lb[16];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    uint8_t icmp[64];
    uint8_t ip6[128];
    uint8_t frame[256];
    uint8_t rx[256];
    size_t icmp_len;
    size_t ip6_len;
    size_t frame_len;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_result_t rc;

    fl_net_loopback_reset();
    fl_net_ipv6_loopback_addr(lb);
    icmp_len = fl_net_icmpv6_echo_request_build(icmp, sizeof(icmp), 0x6a6au, 1u, 8u);
    ASSERT(icmp_len > 0);
    ip6_len = fl_net_ipv6_build(lb, lb, FL_NET_IP_PROTO_ICMPV6, icmp, icmp_len, ip6, sizeof(ip6));
    ASSERT(ip6_len > 0);
    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);
    frame_len = fl_net_wire_build_eth_ipv6(frame, sizeof(frame), peer_mac, host_mac, ip6, ip6_len);
    ASSERT(frame_len > 0);
    view.data = frame;
    view.len = frame_len;
    ASSERT(fl_net_netdev_send(fl_net_netdev_loopback(), &view) == FL_RESULT_OK);
    mut.data = rx;
    mut.cap = sizeof(rx);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    {
        size_t off = 0;
        size_t iplen = 0;
        ASSERT(fl_net_wire_parse_eth_ipv6(rx, mut.len, &off, &iplen));
        ASSERT(fl_net_icmpv6_echo_reply_match(rx + off + FL_NET_IPV6_HDR_LEN,
                                              iplen - FL_NET_IPV6_HDR_LEN, 0x6a6au, 1u));
    }
    return 0;
}

static int test_ipv6_ndp_ns_na_loopback(void) {
    uint8_t lb[16];
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    uint8_t icmp[64];
    uint8_t ip6[128];
    uint8_t frame[256];
    uint8_t rx[256];
    size_t icmp_len;
    size_t ip6_len;
    size_t frame_len;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_result_t rc;

    fl_net_loopback_reset();
    fl_net_ipv6_loopback_addr(lb);
    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);
    icmp_len = fl_net_ndp_build_neighbor_solicit(icmp, sizeof(icmp), lb, peer_mac);
    ASSERT(icmp_len > 0);
    ip6_len = fl_net_ipv6_build(lb, lb, FL_NET_IP_PROTO_ICMPV6, icmp, icmp_len, ip6, sizeof(ip6));
    ASSERT(ip6_len > 0);
    frame_len = fl_net_wire_build_eth_ipv6(frame, sizeof(frame), peer_mac, host_mac, ip6, ip6_len);
    ASSERT(frame_len > 0);
    view.data = frame;
    view.len = frame_len;
    ASSERT(fl_net_netdev_send(fl_net_netdev_loopback(), &view) == FL_RESULT_OK);
    mut.data = rx;
    mut.cap = sizeof(rx);
    mut.len = 0;
    rc = fl_net_netdev_recv(fl_net_netdev_loopback(), &mut, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    {
        size_t off = 0;
        size_t iplen = 0;
        const uint8_t *na;
        ASSERT(fl_net_wire_parse_eth_ipv6(rx, mut.len, &off, &iplen));
        na = rx + off + FL_NET_IPV6_HDR_LEN;
        ASSERT(na[0] == (uint8_t)FL_NET_ICMPV6_TYPE_NA);
    }
    return 0;
}

static int test_ipv6_route_lookup_loopback(void) {
    uint8_t lb[16];
    fl_net_route6_entry_t route;

    fl_net_route_init();
    fl_net_ipv6_loopback_addr(lb);
    ASSERT(fl_net_route_lookup6(lb, &route) == FL_RESULT_OK);
    ASSERT(route.drv == fl_net_netdev_loopback());
    ASSERT(route.prefix_len == 128u);
    return 0;
}

static int test_dns_resolve_aaaa_localhost(void) {
    uint8_t out6[16];
    uint8_t expect[16];
    char txt[64];

    fl_net_ipv6_loopback_addr(expect);
    ASSERT(fl_net_dns_resolve_aaaa("localhost", out6, txt, sizeof(txt)) == FL_RESULT_OK);
    ASSERT(memcmp(out6, expect, 16) == 0);
    ASSERT(strstr(txt, "::1") != NULL || txt[0] != '\0');
    return 0;
}

static int test_tap_smoke(void) {
    const char *skip;
    /* IEEE 802.3 min without FCS; short TAP writes fail with EINVAL on some kernels. */
    uint8_t frame[60];
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_result_t rc;

    skip = getenv(FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME);
    if (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_TAP_VALUE) == 0) {
        fprintf(stderr, "skip: %s=%s\n", FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME, skip);
        return 0;
    }

    ASSERT(strcmp(FL_NET_TAP_IFNAME_PATTERN, "fl0") != 0);

    fl_net_netdev_init();
    rc = fl_net_netdev_tap_open(NULL);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "skip: TAP open (%s)\n", fl_net_netdev_tap_last_error());
        return 0;
    }

    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);
    memset(frame + 6, 0x02, 6);
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0;
    frame[15] = 0x01;
    view.data = frame;
    view.len = sizeof(frame);
    rc = fl_net_netdev_send(fl_net_netdev_tap(), &view);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "skip: TAP send rc=%d (%s)\n", (int)rc,
                fl_net_netdev_tap_last_error());
        fl_net_netdev_tap_close();
        return 0;
    }

    mut.data = frame;
    mut.cap = sizeof(frame);
    mut.len = 0;
    (void)fl_net_netdev_recv(fl_net_netdev_tap(), &mut, 100u);

    fl_net_netdev_tap_close();
    return 0;
}

static int test_packet_pipeline(void) {
    uint8_t buf[128];
    uint8_t ip[40];
    uint8_t l4_out[64];
    size_t frame_len;
    size_t l4_len = 0;
    fl_net_packet_t pkt;
    fl_net_pipeline_rx_t pipe;

    memset(ip, 0, sizeof(ip));
    ip[0] = 0x45;
    ip[9] = FL_NET_IP_PROTO_ICMP;
    ip[12] = 127;
    ip[16] = 127;
    ip[19] = 1;
    ip[2] = 0;
    ip[3] = 40;

    {
        uint8_t mac[6];
        fl_net_loopback_mac_host(mac);
        frame_len = fl_net_wire_build_eth_ipv4(buf, sizeof(buf), mac, mac, ip, 40u);
    }
    ASSERT(frame_len > FL_NET_ETH_FRAME_HDR_LEN);
    ASSERT(FL_NET_PACKET_IMPL_DEFINED == 1);

    ASSERT(fl_net_packet_parse_eth_ipv4(buf, frame_len, &pkt) == FL_RESULT_OK);
    ASSERT((pkt.valid & FL_NET_PKT_VALID_L2) != 0);
    ASSERT((pkt.valid & FL_NET_PKT_VALID_IPV4) != 0);
    ASSERT((pkt.valid & FL_NET_PKT_VALID_L4) != 0);
    ASSERT(pkt.ip_proto == FL_NET_IP_PROTO_ICMP);
    ASSERT(pkt.l4.len == 20u);
    ASSERT(fl_net_packet_copy_l4(&pkt, l4_out, sizeof(l4_out), &l4_len) == FL_RESULT_OK);
    ASSERT(l4_len == 20u);

    fl_net_pipeline_rx_reset(&pipe);
    ASSERT(fl_net_pipeline_rx_feed(&pipe, FL_NET_PIPE_STAGE_PARSE_L4, buf, frame_len) ==
           FL_RESULT_OK);
    ASSERT(pipe.stage == FL_NET_PIPE_STAGE_PARSE_L4);
    ASSERT((pipe.pkt.valid & FL_NET_PKT_VALID_L4) != 0);

    /* Crafted slice must not pass overflow-prone bounds (off + len wrap). */
    pkt.l4.off = pkt.frame.len - 1u;
    pkt.l4.len = 8u;
    ASSERT(fl_net_packet_copy_l4(&pkt, l4_out, sizeof(l4_out), &l4_len) == FL_RESULT_ERR);

    fl_net_pipeline_rx_reset(&pipe);
    ASSERT(fl_net_pipeline_rx_feed(&pipe, FL_NET_PIPE_STAGE_DRV_RX, buf, frame_len) ==
           FL_RESULT_OK);
    ASSERT(pipe.pkt.valid == 0u);

    {
        uint8_t udp_payload[8] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03, 0x04};
        fl_net_packet_t l4_only;

        ASSERT(fl_net_packet_bind_l4(&l4_only, udp_payload, sizeof(udp_payload), 0u,
                                     sizeof(udp_payload)) == FL_RESULT_OK);
        ASSERT((l4_only.valid & FL_NET_PKT_VALID_L4) != 0);
        l4_len = 0;
        ASSERT(fl_net_packet_copy_l4(&l4_only, l4_out, sizeof(l4_out), &l4_len) == FL_RESULT_OK);
        ASSERT(l4_len == sizeof(udp_payload));
        ASSERT(memcmp(l4_out, udp_payload, l4_len) == 0);
    }

    return 0;
}

static int test_net_task_backend_packet_delivery(void) {
    fl_result_t rc;
    uint8_t frame[128];
    uint8_t out[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
    size_t out_len = 0;
    const char payload[] = "user->user payload";
    size_t payload_len = sizeof(payload) - 1u;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 64u, payload, payload_len);

    fl_net_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.frame.data = frame;
    pkt.frame.len = sizeof(frame);
    pkt.l4.off = 64u;
    pkt.l4.len = payload_len;
    pkt.valid = FL_NET_PKT_VALID_L4;

    rc = fl_net_task_backend_user_open(0u, 4u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_task_backend_user_open(1u, 4u);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_task_backend_send_packet(1u, &pkt);
    ASSERT(rc == FL_RESULT_OK);

    out_len = 0;
    rc = fl_net_task_backend_recv_packet(1u, out, sizeof(out), &out_len);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(out_len == payload_len);
    ASSERT(memcmp(out, payload, payload_len) == 0);

    fl_net_task_backend_user_close(0u);
    fl_net_task_backend_user_close(1u);
    return 0;
}

static int test_net_task_backend_server_relay(void) {
    fl_result_t rc;
    uint8_t frame[128];
    uint8_t out_a[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
    uint8_t out_b[FL_NET_TASK_BACKEND_INBOX_PAYLOAD_MAX];
    size_t out_len = 0;
    const char payload[] = "client->server->clients";
    size_t payload_len = sizeof(payload) - 1u;

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 32u, payload, payload_len);

    fl_net_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.frame.data = frame;
    pkt.frame.len = sizeof(frame);
    pkt.l4.off = 32u;
    pkt.l4.len = payload_len;
    pkt.valid = FL_NET_PKT_VALID_L4;

    rc = fl_net_task_backend_user_open(0u, 4u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_task_backend_user_open(1u, 4u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_task_backend_user_open(2u, 4u);
    ASSERT(rc == FL_RESULT_OK);

    /* Client 0 -> server hub -> clients 1 and 2 (server shell TODO in P3-13). */
    rc = fl_net_task_backend_server_ingress(0u, &pkt);
    ASSERT(rc == FL_RESULT_OK);

    out_len = 0;
    rc = fl_net_task_backend_recv_packet(1u, out_a, sizeof(out_a), &out_len);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(out_len == payload_len);
    ASSERT(memcmp(out_a, payload, payload_len) == 0);

    out_len = 0;
    rc = fl_net_task_backend_recv_packet(2u, out_b, sizeof(out_b), &out_len);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(out_len == payload_len);
    ASSERT(memcmp(out_b, payload, payload_len) == 0);

    out_len = 0;
    rc = fl_net_task_backend_recv_packet(0u, out_a, sizeof(out_a), &out_len);
    ASSERT(rc == FL_RESULT_TIMEDOUT);

    fl_net_task_backend_user_close(0u);
    fl_net_task_backend_user_close(1u);
    fl_net_task_backend_user_close(2u);
    return 0;
}

static int test_net_udp_demux_queue(void) {
    fl_net_udp_rx_meta_t meta;
    size_t out_len = 0;
    fl_result_t rc;
    const char payload[] = "demux-payload";
    uint32_t loopback = fl_net_htonl(0x7F000001u);

    fl_net_udp_demux_reset();
    rc = fl_net_udp_bind_port(47001u);
    ASSERT(rc == FL_RESULT_OK);

    {
        uint8_t payload_buf[32];
        fl_net_packet_t payload_pkt;

        memcpy(payload_buf, payload, sizeof(payload) - 1u);
        ASSERT(fl_net_packet_bind_l4(&payload_pkt, payload_buf, sizeof(payload_buf), 0u,
                                     sizeof(payload) - 1u) == FL_RESULT_OK);
        rc = fl_net_udp_deliver_inbound_pkt(loopback, 48000u, 47001u, &payload_pkt);
        ASSERT(rc == FL_RESULT_OK);
    }

    {
        fl_net_packet_t rx_pkt;
        uint8_t rx_buf[64];
        const uint8_t *rx_ptr;

        rc = fl_net_udp_recv_from_port_pkt(47001u, &meta, &rx_pkt, rx_buf, sizeof(rx_buf));
        ASSERT(rc == FL_RESULT_OK);
        rc = fl_net_packet_l4_view(&rx_pkt, &rx_ptr, &out_len);
        ASSERT(rc == FL_RESULT_OK);
        ASSERT(out_len == sizeof(payload) - 1u);
        ASSERT(memcmp(rx_ptr, payload, out_len) == 0);
        ASSERT(meta.dst_port_host == 47001u);
    }

    fl_net_udp_unbind_port(47001u);
    return 0;
}

static int test_net_socket_tcp_loopback(void) {
    fl_net_sock_handle_t listen_h = FL_NET_SOCK_INVALID;
    fl_net_sock_handle_t client_h = FL_NET_SOCK_INVALID;
    fl_net_sock_handle_t accepted_h = FL_NET_SOCK_INVALID;
    fl_result_t rc;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const char msg[] = "tcp-loopback-prep";
    char rx[32];
    size_t sent = 0;
    size_t got = 0;

    rc = fl_net_sock_init();
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        fl_net_sock_shutdown();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_STREAM, &listen_h);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        fl_net_sock_shutdown();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_bind(listen_h, loopback, 48777u);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_sock_listen(listen_h, FL_NET_SOCK_DEFAULT_LISTEN_BACKLOG);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_STREAM, &client_h);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_sock_connect(client_h, loopback, 48777u);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_accept(listen_h, &accepted_h);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_send(client_h, msg, sizeof(msg) - 1u, &sent);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(sent == sizeof(msg) - 1u);

    rc = fl_net_sock_recv(accepted_h, rx, sizeof(rx), &got, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(got == sizeof(msg) - 1u);
    ASSERT(memcmp(rx, msg, got) == 0);

    fl_net_sock_close(client_h);
    fl_net_sock_close(accepted_h);
    fl_net_sock_close(listen_h);
    fl_net_sock_shutdown();
    return 0;
}

/* End-to-end BSD shim coverage for fl_socket / fl_bind / fl_send / fl_recv
 * via FL_NET_SOCK_TYPE_DGRAM — the same surface udpsend/udplisten use. */
static int test_net_socket_udp_loopback(void) {
    fl_net_sock_handle_t listen_h = FL_NET_SOCK_INVALID;
    fl_net_sock_handle_t send_h = FL_NET_SOCK_INVALID;
    fl_result_t rc;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const char msg[] = "udp-loopback-shim";
    char rx[64];
    size_t sent = 0u;
    size_t got = 0u;

    rc = fl_net_sock_init();
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        fl_net_sock_shutdown();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_DGRAM, &listen_h);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        fl_net_sock_shutdown();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_bind(listen_h, loopback, 48923u);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_open(FL_NET_SOCK_TYPE_DGRAM, &send_h);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_sock_connect(send_h, loopback, 48923u);
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_sock_send(send_h, msg, sizeof(msg) - 1u, &sent);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(sent == sizeof(msg) - 1u);

    rc = fl_net_sock_recv(listen_h, rx, sizeof(rx), &got, 2000u);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(got == sizeof(msg) - 1u);
    ASSERT(memcmp(rx, msg, got) == 0);

    fl_net_sock_close(send_h);
    fl_net_sock_close(listen_h);
    fl_net_sock_shutdown();
    return 0;
}

static int test_net_udp_build_datagram(void) {
    uint8_t buf[64];
    uint8_t payload_buf[8];
    const char payload[] = "udp";
    fl_net_packet_t payload_pkt;
    size_t len;
    uint32_t loopback = fl_net_htonl(0x7F000001u);

    memcpy(payload_buf, payload, sizeof(payload) - 1u);
    ASSERT(fl_net_packet_bind_l4(&payload_pkt, payload_buf, sizeof(payload_buf), 0u,
                                 sizeof(payload) - 1u) == FL_RESULT_OK);

    len = fl_net_udp_build_datagram(buf, sizeof(buf), loopback, loopback, 48001u, 7777u,
                                    (const uint8_t *)payload, sizeof(payload) - 1u);
    ASSERT(len == (size_t)FL_NET_UDP_HDR_LEN + sizeof(payload) - 1u);

    len = fl_net_udp_build_datagram_from_pkt(buf, sizeof(buf), loopback, loopback, 48001u, 7777u,
                                             &payload_pkt);
    ASSERT(len == (size_t)FL_NET_UDP_HDR_LEN + sizeof(payload) - 1u);
    ASSERT(buf[0] == (uint8_t)(48001u >> 8));
    ASSERT(buf[1] == (uint8_t)(48001u & 0xff));
    ASSERT(buf[2] == (uint8_t)(7777u >> 8));
    ASSERT(buf[3] == (uint8_t)(7777u & 0xff));
    return 0;
}

static int test_net_task_backend_socket_send_smoke(void) {
    fl_net_socket_endpoint_t ep;
    fl_result_t rc;
    uint32_t loopback = fl_net_htonl(0x7F000001u);
    const char payload[] = "socket arp blend";

    ep.local_ip_be = loopback;
    ep.local_port_be = fl_net_htons(48002u);
    ep.peer_ip_be = loopback;
    ep.peer_port_be = fl_net_htons((uint16_t)FL_NET_TASK_BACKEND_WIRE_SERVER_PORT);

    rc = fl_net_task_backend_socket_send(&ep, (const uint8_t *)payload, sizeof(payload) - 1u);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: socket send unavailable on this platform\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    return 0;
}

static int test_net_task_backend_client_wire_send_smoke(void) {
    fl_result_t rc;
    uint8_t frame[128];
    const char payload[] = "wire egress smoke";
    size_t payload_len = sizeof(payload) - 1u;
    fl_net_packet_t pkt;
    uint32_t loopback = fl_net_htonl(0x7F000001u);

    memset(frame, 0, sizeof(frame));
    memcpy(frame + 16u, payload, payload_len);
    memset(&pkt, 0, sizeof(pkt));
    pkt.frame.data = frame;
    pkt.frame.len = sizeof(frame);
    pkt.l4.off = 16u;
    pkt.l4.len = payload_len;
    pkt.valid = FL_NET_PKT_VALID_L4;

    rc = fl_net_task_backend_client_wire_send(loopback, 48001u,
                                              FL_NET_TASK_BACKEND_WIRE_SERVER_PORT, &pkt);
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: client wire send unavailable on this platform\n");
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    return 0;
}

static int test_net_arp_cache_sweep(void) {
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 1};
    fl_result_t kick;

    fl_net_arp_clear();
    ASSERT(fl_net_arp_cache_sweep(1u) == 0u);
    ASSERT(fl_net_arp_cache_insert(fl_net_htonl(0x0A000001u), mac) == FL_RESULT_OK);
    (void)fl_net_arp_cache_sweep(100000u);

    fl_net_background_init();
    kick = fl_net_background_arp_tick_kick();
    ASSERT(kick == FL_RESULT_OK);
    return 0;
}

static int test_net_arp_tick_stale_eviction(void) {
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint8_t out_mac[6];
    uint32_t ip = (10u << 24) | 2u | (15u << 8); /* 10.0.2.15 */
    unsigned elapsed;
    unsigned removed;

    fl_net_arp_clear();
    ASSERT(fl_net_arp_cache_insert(ip, mac) == FL_RESULT_OK);
    ASSERT(fl_net_arp_cache_lookup(ip, out_mac) != 0);

    elapsed = FL_NET_ARP_TICK_PERIOD_MS * (FL_NET_ARP_CACHE_STALE_TICKS + 3u);
    removed = fl_net_arp_tick(elapsed);
    ASSERT(removed >= 1u);
    ASSERT(fl_net_arp_cache_lookup(ip, out_mac) == 0);
    return 0;
}

static int test_net_dhcp_frame_codec(void) {
    uint8_t req[320];
    uint8_t reply[320];
    size_t len = 0;
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    uint32_t xid = 0xAABBCCDDu;
    uint32_t parsed_xid = 0;
    uint32_t yi = (10u << 24) | 9u; /* 10.0.0.9 (yiaddr, network byte order) */
    uint32_t parsed_yi = 0;
    uint8_t msg = 0;
    fl_net_packet_t req_pkt;
    fl_net_packet_t reply_pkt;
    const uint8_t *l4;
    size_t l4_len = 0;
    fl_result_t rc;

    rc = fl_net_dhcp_build_request_pkt(&req_pkt, req, sizeof(req), FL_NET_DHCP_MSG_DISCOVER, xid,
                                       mac);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT((req_pkt.valid & FL_NET_PKT_VALID_L4) != 0);
    rc = fl_net_packet_l4_view(&req_pkt, &l4, &l4_len);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(l4_len > FL_NET_BOOTP_FIXED_LEN);

    memset(reply, 0, sizeof(reply));
    memcpy(reply, l4, l4_len);
    reply[0] = 2;
    reply[16] = (uint8_t)((yi >> 24) & 0xffu);
    reply[17] = (uint8_t)((yi >> 16) & 0xffu);
    reply[18] = (uint8_t)((yi >> 8) & 0xffu);
    reply[19] = (uint8_t)(yi & 0xffu);
    {
        size_t o = FL_NET_BOOTP_FIXED_LEN;
        reply[o++] = 0x63;
        reply[o++] = 0x82;
        reply[o++] = 0x53;
        reply[o++] = 0x63;
        reply[o++] = FL_NET_DHCP_OPT_MESSAGE_TYPE;
        reply[o++] = 1;
        reply[o++] = FL_NET_DHCP_MSG_OFFER;
        reply[o++] = FL_NET_DHCP_OPT_END;
        len = o;
    }

    rc = fl_net_packet_bind_l4(&reply_pkt, reply, sizeof(reply), 0u, len);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_dhcp_parse_reply_pkt(&reply_pkt, &parsed_xid, &parsed_yi, &msg);
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(parsed_xid == xid);
    ASSERT(parsed_yi == yi);
    ASSERT(msg == FL_NET_DHCP_MSG_OFFER);
    return 0;
}


static int test_net_http_parse_status(void) {
    const char *hdr = "HTTP/1.1 200 OK\r\n";
    const char *full =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 42\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";
    int code = 0;
    size_t clen = 0;

    ASSERT(fl_net_http_parse_status(hdr, strlen(hdr), &code) == FL_RESULT_OK);
    ASSERT(code == 200);
    ASSERT(fl_net_http_parse_status("HTTP/1.0 404 Not Found\r\n", 22, &code) == FL_RESULT_OK);
    ASSERT(code == 404);
    ASSERT(fl_net_http_parse_status(hdr, 11, &code) == FL_RESULT_ERR);

    ASSERT(fl_net_http_parse_content_length(full, strlen(full), &clen) == FL_RESULT_OK);
    ASSERT(clen == 42u);
    ASSERT(fl_net_http_transfer_encoding_is_chunked(full, strlen(full)) != 0);
    ASSERT(fl_net_http_parse_content_length(hdr, strlen(hdr), &clen) == FL_RESULT_NOENT);
    return 0;
}

static int test_net_tftp_build_rrq(void) {
    uint8_t buf[80];
    size_t n;

    n = fl_net_tftp_build_rrq(buf, sizeof(buf), "boot.bin", "octet");
    ASSERT(n > 6u);
    ASSERT(buf[0] == 0 && buf[1] == 1);
    return 0;
}

static int test_route_zero_prefix_policy(void) {
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 1};
    uint32_t catch_all_be = 0;

    fl_net_route_clear();
    ASSERT(fl_net_route_add(0u, 0u, 0x0200000au, fl_net_netdev_loopback(),
                            fl_net_htonl(0x7F000001u), mac) ==
           FL_RESULT_OK);
    fl_net_route_clear();
    ASSERT(fl_net_ipv4_parse_literal("1.2.3.4", &catch_all_be));
    ASSERT(fl_net_route_add(catch_all_be, 0u, 0, fl_net_netdev_loopback(), catch_all_be, mac) ==
           FL_RESULT_INVAL);
    return 0;
}

static int test_net_tls_openssl_bridge(void) {
    ASSERT(fl_net_tls_hosted_max_plaintext_record() == (size_t)FL_NET_TLS_MAX_PLAINTEXT_RECORD);
    if (!fl_net_tls_hosted_openssl_available()) {
        fl_net_tls_session_t sess = FL_NET_TLS_SESSION_INVALID;
        ASSERT(fl_net_tls_hosted_client_connect(-1, "localhost", &sess) == FL_RESULT_NOSYS);
        return 0;
    }
    return 0;
}

static int test_net_tls_hosted_cap(void) {
    ASSERT(fl_net_tls_hosted_max_plaintext_record() == (size_t)FL_NET_TLS_MAX_PLAINTEXT_RECORD);
    return 0;
}

static int test_net_tcp_stream_listen_connect(void) {
    fl_net_sock_handle_t listen_h = FL_NET_SOCK_INVALID;
    fl_net_sock_handle_t client_h = FL_NET_SOCK_INVALID;
    fl_net_sock_handle_t accepted_h = FL_NET_SOCK_INVALID;
    fl_result_t rc;
    uint32_t loopback = fl_net_htonl(0x7F000001u);

    rc = fl_net_tcp_stream_listen(loopback, 48778u, &listen_h);
    if (rc == FL_RESULT_NOSYS) {
        fl_net_sock_shutdown();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

    rc = fl_net_tcp_stream_connect(loopback, 48778u, &client_h);
    ASSERT(rc == FL_RESULT_OK);
    rc = fl_net_tcp_stream_accept(listen_h, &accepted_h);
    ASSERT(rc == FL_RESULT_OK);

    fl_net_sock_close(client_h);
    fl_net_sock_close(accepted_h);
    fl_net_sock_close(listen_h);
    fl_net_sock_shutdown();
    return 0;
}

static int test_netdev_shutdown(void) {
    uint8_t host_mac[6];
    uint8_t peer_mac[6];
    uint8_t icmp[64];
    uint8_t ip[128];
    uint8_t frame[192];
    fl_net_frame_view_t view;
    size_t icmp_len;
    size_t ip_len;
    size_t frame_len;
    uint32_t dst_be = fl_net_htonl(0x7F000001u);
    uint32_t src_be = fl_net_htonl(0x7F000001u);
    fl_net_ipv4_hdr_t hdr;

    fl_net_netdev_init();
    fl_net_netdev_shutdown();
    ASSERT(fl_net_netdev_tap_is_open() == 0);
    fl_net_netdev_init();
    icmp_len = fl_net_icmp_echo_request_build(icmp, sizeof(icmp), 0x4242u, 1u, 8u);
    ASSERT(icmp_len > 0);
    ip_len = fl_net_ipv4_build(&hdr, ip, sizeof(ip), FL_NET_IP_PROTO_ICMP, src_be, dst_be, icmp,
                               icmp_len, 0x1001u);
    ASSERT(ip_len > 0);
    fl_net_loopback_mac_host(host_mac);
    fl_net_loopback_mac_peer(peer_mac);
    frame_len = fl_net_eth_build_ipv4(frame, sizeof(frame), peer_mac, host_mac, ip, ip_len);
    ASSERT(frame_len > 0);
    view.data = frame;
    view.len = frame_len;
    ASSERT(fl_net_netdev_send(fl_net_netdev_loopback(), &view) == FL_RESULT_OK);
    fl_net_netdev_shutdown();
    ASSERT(fl_net_loopback_stat_tx() == 0u);
    return 0;
}

static int test_route_configure_static(void) {
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    fl_net_route_entry_t route;
    uint32_t dst = 0;

    fl_net_route_clear();
    ASSERT(fl_net_route_configure_static(fl_net_netdev_loopback(), mac, "10.0.2.15", 24u,
                                         "10.0.2.2") == FL_RESULT_OK);
    ASSERT(fl_net_ipv4_parse_literal("10.0.2.15", &dst));
    ASSERT(fl_net_route_lookup(dst, &route) == FL_RESULT_OK);
    ASSERT(route.drv == fl_net_netdev_loopback());
    ASSERT(route.prefix_len == 24u);
    ASSERT(fl_net_route_configure_static(fl_net_netdev_loopback(), mac, "10.0.2.15", 0u,
                                         "10.0.2.2") == FL_RESULT_INVAL);
    ASSERT(fl_net_route_configure_static(fl_net_netdev_loopback(), mac, "10.0.2.15", 33u,
                                         "10.0.2.2") == FL_RESULT_INVAL);
    return 0;
}

static int test_probe_endpoint(void) {
    fl_net_requirements_report_t rep;
    fl_result_t prc = fl_net_probe_endpoint("127.0.0.1", 9, 3000u, &rep);
    ASSERT(prc == FL_RESULT_OK);
    if (getenv("SKIP_NETWORK_INTEROP") &&
        !strcmp(getenv("SKIP_NETWORK_INTEROP"), "1")) {
        return 0;
    }
    ASSERT(rep.ok == 1);
    ASSERT(strstr(rep.endpoint, "127.0.0.1") != NULL);
    return 0;
}

static int test_wifi_lab_server_host(void) {
    fl_net_server_t srv;
    fl_net_wifi_cred_t cred;
    fl_result_t rc;
    uint32_t host_be = 0u;
    uint16_t port = (uint16_t)(49000u + (unsigned)(getpid() % 1000u));

    (void)unsetenv("FL_NET_SOCK_NATIVE");
    (void)setenv("FL_NET_WIFI_USE_WPA", "0", 1);
    (void)setenv("FL_NET_WIFI_LAB", "1", 1);
    fl_net_route_init();
    ASSERT(fl_net_wifi_station_init() == FL_RESULT_OK);
    ASSERT(fl_net_wifi_scan(FL_WIFI_BAND_ANY, 1000u) == FL_RESULT_OK);
    memset(&cred, 0, sizeof(cred));
    strncpy(cred.ssid, "LabAxHome", sizeof(cred.ssid) - 1u);
    strncpy(cred.passphrase, "secret", sizeof(cred.passphrase) - 1u);
    cred.auth_mode = FL_WIFI_AUTH_WPA3_SAE;
    ASSERT(fl_net_wifi_connect(&cred, 5000u) == FL_RESULT_OK);
    ASSERT(fl_net_wifi_netdev_ipv4(&host_be) == FL_RESULT_OK);
    ASSERT(host_be != 0u);
    ASSERT(fl_net_sock_native_eligible_bind_v4(host_be));
    rc = fl_net_sock_init();
    if (rc == FL_RESULT_NOSYS) {
        fprintf(stderr, "skip: hosted sockets unavailable\n");
        (void)fl_net_wifi_disconnect();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);
    ASSERT(fl_net_server_host_start(&srv, host_be, port, "LabHost") == FL_RESULT_OK);
    fl_net_server_host_stop(&srv);
    fl_net_sock_shutdown();
    ASSERT(fl_net_wifi_disconnect() == FL_RESULT_OK);
    return 0;
}

int main(void) {
    fl_net_netdev_init();

    printf("test_wire_vocabulary... ");
    if (test_wire_vocabulary() != 0)
        return 1;
    puts("ok");

    printf("test_ipv4_prefix_match... ");
    if (test_ipv4_prefix_match() != 0)
        return 1;
    puts("ok");

    printf("test_arp_cache_and_frame... ");
    if (test_arp_cache_and_frame() != 0)
        return 1;
    puts("ok");

    printf("test_packet_pipeline... ");
    if (test_packet_pipeline() != 0)
        return 1;
    puts("ok");

    printf("test_net_task_backend_packet_delivery... ");
    if (test_net_task_backend_packet_delivery() != 0)
        return 1;
    puts("ok");

    printf("test_net_task_backend_server_relay... ");
    if (test_net_task_backend_server_relay() != 0)
        return 1;
    puts("ok");

    printf("test_net_task_backend_client_wire_send_smoke... ");
    if (test_net_task_backend_client_wire_send_smoke() != 0)
        return 1;
    puts("ok");

    printf("test_net_udp_demux_queue... ");
    if (test_net_udp_demux_queue() != 0)
        return 1;
    puts("ok");

    printf("test_net_udp_build_datagram... ");
    if (test_net_udp_build_datagram() != 0)
        return 1;
    puts("ok");

    printf("test_net_socket_tcp_loopback... ");
    if (test_net_socket_tcp_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_net_socket_udp_loopback... ");
    if (test_net_socket_udp_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_net_arp_cache_sweep... ");
    if (test_net_arp_cache_sweep() != 0)
        return 1;
    puts("ok");

    printf("test_net_arp_tick_stale_eviction... ");
    if (test_net_arp_tick_stale_eviction() != 0)
        return 1;
    puts("ok");

    printf("test_net_dhcp_frame_codec... ");
    if (test_net_dhcp_frame_codec() != 0)
        return 1;
    puts("ok");

    printf("test_net_http_parse_status... ");
    if (test_net_http_parse_status() != 0)
        return 1;
    puts("ok");

    printf("test_net_tftp_build_rrq... ");
    if (test_net_tftp_build_rrq() != 0)
        return 1;
    puts("ok");

    printf("test_route_zero_prefix_policy... ");
    if (test_route_zero_prefix_policy() != 0)
        return 1;
    puts("ok");

    printf("test_net_tls_openssl_bridge... ");
    if (test_net_tls_openssl_bridge() != 0)
        return 1;
    puts("ok");

    printf("test_net_tls_hosted_cap... ");
    if (test_net_tls_hosted_cap() != 0)
        return 1;
    puts("ok");

    printf("test_net_tcp_stream_listen_connect... ");
    if (test_net_tcp_stream_listen_connect() != 0)
        return 1;
    puts("ok");

    printf("test_net_task_backend_socket_send_smoke... ");
    if (test_net_task_backend_socket_send_smoke() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_arp_exchange... ");
    if (test_loopback_arp_exchange() != 0)
        return 1;
    puts("ok");

    printf("test_icmp_echo_asm_layout... ");
    if (test_icmp_echo_asm_layout() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_octet... ");
    if (test_loopback_octet() != 0)
        return 1;
    puts("ok");

    printf("test_resolve_localhost... ");
    if (test_resolve_localhost() != 0)
        return 1;
    puts("ok");

    printf("test_icmp_unreachable_no_linux_fallback... ");
    if (test_icmp_unreachable_no_linux_fallback() != 0)
        return 1;
    puts("ok");

    printf("test_udp_unreachable_no_linux_fallback... ");
    if (test_udp_unreachable_no_linux_fallback() != 0)
        return 1;
    printf("ok\n");

        printf("test_udp_echo_loopback... ");
    if (test_udp_echo_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_tcp_connect_rst_slot_release... ");
    if (test_tcp_connect_rst_slot_release() != 0)
        return 1;
    printf("ok\n");

    printf("test_tcp_dual_connect_loopback... ");
    if (test_tcp_dual_connect_loopback() != 0)
        return 1;
    printf("ok\n");

        printf("test_tcp_fsm_loopback... ");
    if (test_tcp_fsm_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_ping... ");
    if (test_loopback_ping() != 0)
        return 1;
    puts("ok");

    printf("test_loopback_tcp... ");
    if (test_loopback_tcp() != 0)
        return 1;
    puts("ok");

    printf("test_probe_endpoint... ");
    if (test_probe_endpoint() != 0)
        return 1;
    puts("ok");

    printf("test_netdev_loopback_frame... ");
    if (test_netdev_loopback_frame() != 0)
        return 1;
    puts("ok");

    printf("test_ipv6_icmp_echo_loopback... ");
    if (test_ipv6_icmp_echo_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_ipv6_ndp_ns_na_loopback... ");
    if (test_ipv6_ndp_ns_na_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_ipv6_route_lookup_loopback... ");
    if (test_ipv6_route_lookup_loopback() != 0)
        return 1;
    puts("ok");

    printf("test_dns_resolve_aaaa_localhost... ");
    if (test_dns_resolve_aaaa_localhost() != 0)
        return 1;
    puts("ok");

    printf("test_netdev_shutdown... ");
    if (test_netdev_shutdown() != 0)
        return 1;
    puts("ok");

    printf("test_route_configure_static... ");
    if (test_route_configure_static() != 0)
        return 1;
    puts("ok");

    printf("test_tap_smoke... ");
    if (test_tap_smoke() != 0)
        return 1;
    puts("ok");

    printf("test_wifi_lab_server_host... ");
    if (test_wifi_lab_server_host() != 0)
        return 1;
    puts("ok");

    puts("test_p3_network: all passed");
    return 0;
}
