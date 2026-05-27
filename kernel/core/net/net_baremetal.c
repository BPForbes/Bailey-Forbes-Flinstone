#ifdef DRIVERS_BAREMETAL

#include "net_baremetal.h"

#include "net_arp.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_wire.h"

#include <string.h>

#ifndef FL_NET_LAB_IPV4_DEFAULT
#define FL_NET_LAB_IPV4_DEFAULT "10.0.2.15"
#endif
#ifndef FL_NET_LAB_PREFIX_DEFAULT
#define FL_NET_LAB_PREFIX_DEFAULT 24u
#endif
#ifndef FL_NET_LAB_GW_DEFAULT
#define FL_NET_LAB_GW_DEFAULT "10.0.2.2"
#endif

#define FL_NET_LAB_RX_SLOTS 8u
#define FL_NET_LAB_RX_MAX FL_NET_WIRE_FRAME_BUF_MAX

typedef struct {
    size_t len;
    uint8_t data[FL_NET_LAB_RX_MAX];
} fl_net_lab_rx_slot_t;

static fl_net_driver_t s_lab_drv;
static fl_net_lab_rx_slot_t s_lab_rx[FL_NET_LAB_RX_SLOTS];
static unsigned s_lab_rx_head;
static unsigned s_lab_rx_count;
static uint64_t s_lab_tx;
static uint64_t s_lab_stat_rx;
static int s_lab_up;

static void lab_rx_clear(void) {
    s_lab_rx_head = 0;
    s_lab_rx_count = 0;
}

static fl_result_t lab_rx_enqueue(const uint8_t *frame, size_t len) {
    unsigned idx;

    if (!frame || len == 0 || len > FL_NET_LAB_RX_MAX)
        return FL_RESULT_INVAL;
    if (s_lab_rx_count >= FL_NET_LAB_RX_SLOTS)
        return FL_RESULT_ERR;

    idx = (s_lab_rx_head + s_lab_rx_count) % FL_NET_LAB_RX_SLOTS;
    memcpy(s_lab_rx[idx].data, frame, len);
    s_lab_rx[idx].len = len;
    s_lab_rx_count++;
    return FL_RESULT_OK;
}

static fl_result_t lab_rx_dequeue(uint8_t *buf, size_t cap, size_t *len) {
    if (!buf || !len)
        return FL_RESULT_INVAL;
    if (s_lab_rx_count == 0)
        return FL_RESULT_TIMEDOUT;
    if (cap < s_lab_rx[s_lab_rx_head].len)
        return FL_RESULT_ERR;

    *len = s_lab_rx[s_lab_rx_head].len;
    memcpy(buf, s_lab_rx[s_lab_rx_head].data, *len);
    s_lab_rx_head = (s_lab_rx_head + 1u) % FL_NET_LAB_RX_SLOTS;
    s_lab_rx_count--;
    return FL_RESULT_OK;
}

void fl_net_baremetal_lab_mac(uint8_t mac_out[6]) {
    if (!mac_out)
        return;
    mac_out[0] = 0x02u;
    mac_out[1] = 0x11u;
    mac_out[2] = 0x22u;
    mac_out[3] = 0x33u;
    mac_out[4] = 0x44u;
    mac_out[5] = 0x55u;
}

uint32_t fl_net_baremetal_lab_ipv4_be(void) {
    uint32_t be = 0;

    if (!fl_net_ipv4_parse_literal(FL_NET_LAB_IPV4_DEFAULT, &be))
        return (10u << 24) | (2u << 16) | 15u;
    return be;
}

static fl_result_t lab_driver_send(fl_net_driver_t *drv, const fl_net_frame_view_t *frame) {
    size_t ip_off;
    size_t ip_len;
    uint32_t dst_be;
    uint8_t reply[FL_NET_LAB_RX_MAX];
    size_t reply_len = 0;
    uint32_t local_ip;
    uint8_t local_mac[6];

    (void)drv;

    if (fl_net_wire_check_view(frame, FL_NET_ETH_FRAME_HDR_LEN) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    s_lab_tx++;
    local_ip = fl_net_baremetal_lab_ipv4_be();
    fl_net_baremetal_lab_mac(local_mac);

    {
        int eth_ok = 0;
        uint16_t ethertype = fl_net_wire_ethertype_be16(frame->data, frame->len, &eth_ok);
        if (eth_ok && ethertype == FL_ETHERTYPE_ARP) {
            if (fl_net_arp_input(frame->data, frame->len, local_ip, local_mac, reply, sizeof(reply),
                                 &reply_len) == FL_RESULT_OK && reply_len > 0)
                return lab_rx_enqueue(reply, reply_len);
            return FL_RESULT_OK;
        }
    }

    if (!fl_net_wire_parse_eth_ipv4(frame->data, frame->len, &ip_off, &ip_len, &dst_be))
        return FL_RESULT_OK;

    if (dst_be == local_ip) {
        uint8_t ip_reply[FL_NET_LAB_RX_MAX];
        size_t ip_reply_len;
        uint8_t host_mac[6];
        uint8_t peer_mac[6];
        size_t eth_len;

        if (frame->data[ip_off + 9] == FL_NET_IP_PROTO_ICMP) {
            const uint8_t *icmp = frame->data + ip_off + ((size_t)(frame->data[ip_off] & 0x0fu) * 4u);
            size_t hdr_len = (size_t)(frame->data[ip_off] & 0x0fu) * 4u;
            size_t icmp_len = ip_len - hdr_len;
            uint8_t icmp_reply[FL_NET_LAB_RX_MAX];
            size_t icmp_reply_len = 0;

            if (fl_net_loopback_icmp_echo(icmp, icmp_len, icmp_reply, sizeof(icmp_reply),
                                        &icmp_reply_len) != FL_RESULT_OK)
                return FL_RESULT_OK;
            ip_reply_len = icmp_len; /* built below via loopback_build path */
            (void)ip_reply_len;
            {
                size_t built;
                uint8_t ipbuf[576];
                fl_net_ipv4_hdr_t hdr;
                uint32_t src_be = (uint32_t)frame->data[ip_off + 12] |
                                  ((uint32_t)frame->data[ip_off + 13] << 8) |
                                  ((uint32_t)frame->data[ip_off + 14] << 16) |
                                  ((uint32_t)frame->data[ip_off + 15] << 24);

                built = fl_net_ipv4_build(&hdr, ipbuf, sizeof(ipbuf), FL_NET_IP_PROTO_ICMP, local_ip,
                                          src_be, icmp_reply, icmp_reply_len, 0x4242u);
                if (built == 0)
                    return FL_RESULT_OK;
                fl_net_baremetal_lab_mac(host_mac);
                memcpy(peer_mac, frame->data + 6, 6);
                eth_len = fl_net_wire_build_eth_ipv4(reply, sizeof(reply), host_mac, peer_mac, ipbuf,
                                                     built);
                if (eth_len > 0)
                    return lab_rx_enqueue(reply, eth_len);
            }
        }
    }

    return FL_RESULT_OK;
}

static fl_result_t lab_driver_recv(fl_net_driver_t *drv, fl_net_frame_mut_t *out) {
    fl_result_t rc;

    (void)drv;

    if (fl_net_wire_check_mut(out) != FL_RESULT_OK)
        return FL_RESULT_INVAL;

    rc = lab_rx_dequeue(out->data, out->cap, &out->len);
    if (rc == FL_RESULT_OK) {
        (void)fl_net_wire_check_rx_fill(out, out->len);
        s_lab_stat_rx++;
    }
    return rc;
}

void fl_net_baremetal_init(void) {
    uint8_t mac[6];

    lab_rx_clear();
    s_lab_tx = 0;
    s_lab_stat_rx = 0;
    s_lab_up = 0;

    memset(&s_lab_drv, 0, sizeof(s_lab_drv));
    s_lab_drv.send = lab_driver_send;
    s_lab_drv.recv = lab_driver_recv;
    s_lab_drv.mtu = FL_NET_ETH_MTU_DEFAULT;
    s_lab_drv.advertised_caps = 0;
    s_lab_drv.impl = NULL;

    fl_net_baremetal_lab_mac(mac);
    if (fl_net_route_configure_static(&s_lab_drv, mac, FL_NET_LAB_IPV4_DEFAULT,
                                      FL_NET_LAB_PREFIX_DEFAULT, FL_NET_LAB_GW_DEFAULT) ==
        FL_RESULT_OK) {
        s_lab_up = 1;
        (void)fl_net_arp_send_gratuitous(&s_lab_drv, mac, fl_net_baremetal_lab_ipv4_be());
    }
}

void fl_net_baremetal_shutdown(void) {
    lab_rx_clear();
    s_lab_up = 0;
    s_lab_tx = 0;
    s_lab_stat_rx = 0;
}

fl_net_driver_t *fl_net_netdev_lab(void) {
    return &s_lab_drv;
}

int fl_net_baremetal_lab_is_up(void) {
    return s_lab_up;
}

void fl_net_baremetal_timer_poll(unsigned elapsed_ms) {
    (void)fl_net_arp_tick(elapsed_ms);
}

#else /* !DRIVERS_BAREMETAL */

#include "net_baremetal.h"

void fl_net_baremetal_init(void) {}
void fl_net_baremetal_shutdown(void) {}
fl_net_driver_t *fl_net_netdev_lab(void) { return NULL; }
int fl_net_baremetal_lab_is_up(void) { return 0; }
void fl_net_baremetal_lab_mac(uint8_t mac_out[6]) {
    (void)mac_out;
}
uint32_t fl_net_baremetal_lab_ipv4_be(void) { return 0; }
uint64_t fl_net_baremetal_lab_stat_tx(void) { return 0; }
uint64_t fl_net_baremetal_lab_stat_rx(void) { return 0; }
void fl_net_baremetal_timer_poll(unsigned elapsed_ms) { (void)elapsed_ms; }

#endif /* DRIVERS_BAREMETAL */
