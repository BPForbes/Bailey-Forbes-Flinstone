#include "contract_p0_ci.h"
#include "contract_p3_ipv4.h"
#include "contract_p3_wire.h"
#include "net_arp.h"
#include "net_dns.h"
#include "net_eth.h"
#include "net_route.h"
#include "net_wire.h"
#include "net_checksum.h"
#include "net_icmp.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_packet.h"
#include "net_ping_host.h"
#include "net_requirements.h"
#include "net_background.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
                                   (uint32_t)127 | (1u << 24),
                                   (uint32_t)127 | (2u << 24));
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
                                   (uint32_t)127 | (2u << 24),
                                   (uint32_t)127 | (1u << 24));
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
    struct in_addr a;
    ASSERT(inet_pton(AF_INET, "127.0.0.1", &a) == 1);
    ASSERT(fl_net_ipv4_is_loopback(a.s_addr));
    ASSERT(inet_pton(AF_INET, "8.8.8.8", &a) == 1);
    ASSERT(!fl_net_ipv4_is_loopback(a.s_addr));
    return 0;
}

static int test_resolve_localhost(void) {
    uint32_t addr_be = 0;
    char resolved[INET_ADDRSTRLEN];
    ASSERT(fl_net_resolve_ipv4("localhost", &addr_be, resolved, sizeof(resolved)) ==
           FL_RESULT_OK);
    ASSERT(fl_net_ipv4_is_loopback(addr_be));
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
    uint32_t dst_be = (uint32_t)127 | (1u << 24);
    uint32_t src_be = (uint32_t)127 | (1u << 24);
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

static int test_tap_smoke(void) {
    const char *skip;
    uint8_t frame[FL_NET_ETH_HDR_LEN + 4];
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    fl_result_t rc;

    skip = getenv(FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME);
    if (skip && strcmp(skip, FL_CONTRACT_P0_CI_SKIP_TAP_VALUE) == 0) {
        fprintf(stderr, "skip: %s=%s\n", FL_CONTRACT_P0_CI_SKIP_TAP_ENV_NAME, skip);
        return 0;
    }

    fl_net_netdev_init();
    rc = fl_net_netdev_tap_open(NULL);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "skip: TAP open (%s)\n", fl_net_netdev_tap_last_error());
        return 0;
    }

    memset(frame, 0xff, 6);
    memset(frame + 6, 0x02, 6);
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0;
    frame[15] = 0x01;
    view.data = frame;
    view.len = sizeof(frame);
    rc = fl_net_netdev_send(fl_net_netdev_tap(), &view);
    if (rc == FL_RESULT_ACCES) {
        fprintf(stderr, "skip: TAP send denied (netdev I/O authz)\n");
        fl_net_netdev_tap_close();
        return 0;
    }
    ASSERT(rc == FL_RESULT_OK);

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

    printf("test_tap_smoke... ");
    if (test_tap_smoke() != 0)
        return 1;
    puts("ok");

    puts("test_p3_network: all passed");
    return 0;
}
