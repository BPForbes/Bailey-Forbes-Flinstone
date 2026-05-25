#include "net_arp.h"

#include "contract_p3_arp.h"
#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_wire.h"

#include <string.h>

#define FL_NET_ARP_BCAST_0 0xffu

typedef struct {
    uint32_t ip_be;
    uint8_t mac[FL_NET_ETH_ADDR_LEN];
    unsigned age;
} fl_net_arp_cache_entry_t;

static fl_net_arp_cache_entry_t s_arp_cache[FL_NET_ARP_CACHE_MAX];
static unsigned s_arp_cache_count;
static unsigned s_arp_tick;

void fl_net_arp_init(void) {
    fl_net_arp_clear();
}

void fl_net_arp_clear(void) {
    memset(s_arp_cache, 0, sizeof(s_arp_cache));
    s_arp_cache_count = 0;
    s_arp_tick = 0;
}

static void arp_cache_evict_oldest(void) {
    unsigned i;
    unsigned oldest = 0;

    if (s_arp_cache_count < FL_NET_ARP_CACHE_MAX)
        return;

    for (i = 1; i < FL_NET_ARP_CACHE_MAX; i++) {
        if (s_arp_cache[i].age < s_arp_cache[oldest].age)
            oldest = i;
    }
    if (oldest < s_arp_cache_count - 1u)
        s_arp_cache[oldest] = s_arp_cache[s_arp_cache_count - 1u];
    s_arp_cache_count--;
}

fl_result_t fl_net_arp_cache_insert(uint32_t ipv4_be, const uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    unsigned i;

    if (!mac)
        return FL_RESULT_INVAL;

    for (i = 0; i < s_arp_cache_count; i++) {
        if (s_arp_cache[i].ip_be == ipv4_be) {
            memcpy(s_arp_cache[i].mac, mac, FL_NET_ETH_ADDR_LEN);
            s_arp_cache[i].age = ++s_arp_tick;
            return FL_RESULT_OK;
        }
    }

    if (s_arp_cache_count >= FL_NET_ARP_CACHE_MAX)
        arp_cache_evict_oldest();

    s_arp_cache[s_arp_cache_count].ip_be = ipv4_be;
    memcpy(s_arp_cache[s_arp_cache_count].mac, mac, FL_NET_ETH_ADDR_LEN);
    s_arp_cache[s_arp_cache_count].age = ++s_arp_tick;
    s_arp_cache_count++;
    return FL_RESULT_OK;
}

int fl_net_arp_cache_lookup(uint32_t ipv4_be, uint8_t mac_out[FL_NET_ETH_ADDR_LEN]) {
    unsigned i;

    if (!mac_out)
        return 0;

    for (i = 0; i < s_arp_cache_count; i++) {
        if (s_arp_cache[i].ip_be == ipv4_be) {
            memcpy(mac_out, s_arp_cache[i].mac, FL_NET_ETH_ADDR_LEN);
            s_arp_cache[i].age = ++s_arp_tick;
            return 1;
        }
    }
    return 0;
}

static size_t arp_build_eth(uint8_t *frame, size_t cap, const uint8_t dst_mac[FL_NET_ETH_ADDR_LEN],
                            const uint8_t src_mac[FL_NET_ETH_ADDR_LEN], const uint8_t *arp,
                            size_t arp_len) {
    size_t total;
    fl_net_frame_view_t view;

    if (!frame || !dst_mac || !src_mac || !arp || arp_len != FL_NET_ARP_PKT_LEN)
        return 0;

    total = (size_t)FL_NET_ETH_FRAME_HDR_LEN + arp_len;
    if (cap < total)
        return 0;

    memcpy(frame, dst_mac, FL_NET_ETH_ADDR_LEN);
    memcpy(frame + FL_NET_ETH_ADDR_LEN, src_mac, FL_NET_ETH_ADDR_LEN);
    frame[12] = (uint8_t)(FL_ETHERTYPE_ARP >> 8);
    frame[13] = (uint8_t)(FL_ETHERTYPE_ARP & 0xffu);
    memcpy(frame + FL_NET_ETH_FRAME_HDR_LEN, arp, arp_len);

    view = fl_net_frame_view_make(frame, total);
    if (fl_net_wire_check_tx(&view, FL_NET_ETH_MTU_DEFAULT) != FL_RESULT_OK)
        return 0;
    return total;
}

static size_t arp_build_body(uint8_t *arp, size_t cap, uint16_t op, const uint8_t sha[6],
                             uint32_t spa_be, const uint8_t tha[6], uint32_t tpa_be) {
    if (!arp || cap < FL_NET_ARP_PKT_LEN)
        return 0;

    arp[0] = (uint8_t)(FL_NET_ARP_HWTYPE_ETHERNET >> 8);
    arp[1] = (uint8_t)(FL_NET_ARP_HWTYPE_ETHERNET & 0xffu);
    arp[2] = (uint8_t)(FL_NET_ARP_PROTOTYPE_IPV4 >> 8);
    arp[3] = (uint8_t)(FL_NET_ARP_PROTOTYPE_IPV4 & 0xffu);
    arp[4] = FL_NET_ETH_ADDR_LEN;
    arp[5] = FL_NET_IPV4_ADDR_LEN;
    arp[6] = (uint8_t)(op >> 8);
    arp[7] = (uint8_t)(op & 0xffu);
    memcpy(arp + 8, sha, FL_NET_ETH_ADDR_LEN);
    arp[14] = (uint8_t)(spa_be & 0xffu);
    arp[15] = (uint8_t)((spa_be >> 8) & 0xffu);
    arp[16] = (uint8_t)((spa_be >> 16) & 0xffu);
    arp[17] = (uint8_t)((spa_be >> 24) & 0xffu);
    memcpy(arp + 18, tha, FL_NET_ETH_ADDR_LEN);
    arp[24] = (uint8_t)(tpa_be & 0xffu);
    arp[25] = (uint8_t)((tpa_be >> 8) & 0xffu);
    arp[26] = (uint8_t)((tpa_be >> 16) & 0xffu);
    arp[27] = (uint8_t)((tpa_be >> 24) & 0xffu);
    return FL_NET_ARP_PKT_LEN;
}

static void arp_bcast_mac(uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    memset(mac, FL_NET_ARP_BCAST_0, FL_NET_ETH_ADDR_LEN);
}

size_t fl_net_arp_build_request(uint8_t *frame, size_t cap, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                                uint32_t src_ip_be, uint32_t target_ip_be) {
    uint8_t arp[FL_NET_ARP_PKT_LEN];
    uint8_t zero_mac[FL_NET_ETH_ADDR_LEN];
    uint8_t bcast[FL_NET_ETH_ADDR_LEN];
    size_t arp_len;

    if (!frame || !src_mac)
        return 0;

    memset(zero_mac, 0, sizeof(zero_mac));
    arp_len = arp_build_body(arp, sizeof(arp), FL_NET_ARP_OP_REQUEST, src_mac, src_ip_be, zero_mac,
                             target_ip_be);
    if (arp_len == 0)
        return 0;

    arp_bcast_mac(bcast);
    return arp_build_eth(frame, cap, bcast, src_mac, arp, arp_len);
}

size_t fl_net_arp_build_reply(uint8_t *frame, size_t cap, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                              uint32_t src_ip_be, const uint8_t dst_mac[FL_NET_ETH_ADDR_LEN],
                              uint32_t dst_ip_be) {
    uint8_t arp[FL_NET_ARP_PKT_LEN];
    size_t arp_len;

    if (!frame || !src_mac || !dst_mac)
        return 0;

    arp_len = arp_build_body(arp, sizeof(arp), FL_NET_ARP_OP_REPLY, src_mac, src_ip_be, dst_mac,
                             dst_ip_be);
    if (arp_len == 0)
        return 0;
    return arp_build_eth(frame, cap, dst_mac, src_mac, arp, arp_len);
}

int fl_net_arp_parse_eth(const uint8_t *frame, size_t len, uint16_t *out_op,
                         uint32_t *sender_ip_be, uint8_t sender_mac[FL_NET_ETH_ADDR_LEN],
                         uint32_t *target_ip_be, uint8_t target_mac[FL_NET_ETH_ADDR_LEN]) {
    const uint8_t *arp;
    int ok = 0;
    uint16_t ethertype;

    if (!frame || len < FL_NET_ETH_FRAME_HDR_LEN + FL_NET_ARP_PKT_LEN)
        return 0;

    ethertype = fl_net_wire_ethertype_be16(frame, len, &ok);
    if (!ok || ethertype != FL_ETHERTYPE_ARP)
        return 0;

    arp = frame + FL_NET_ETH_FRAME_HDR_LEN;
    if (((uint16_t)arp[0] << 8 | arp[1]) != FL_NET_ARP_HWTYPE_ETHERNET)
        return 0;
    if (((uint16_t)arp[2] << 8 | arp[3]) != FL_NET_ARP_PROTOTYPE_IPV4)
        return 0;
    if (arp[4] != FL_NET_ETH_ADDR_LEN || arp[5] != FL_NET_IPV4_ADDR_LEN)
        return 0;

    if (out_op)
        *out_op = (uint16_t)(((uint16_t)arp[6] << 8) | arp[7]);
    if (sender_mac)
        memcpy(sender_mac, arp + 8, FL_NET_ETH_ADDR_LEN);
    if (sender_ip_be)
        *sender_ip_be = (uint32_t)arp[14] | ((uint32_t)arp[15] << 8) | ((uint32_t)arp[16] << 16) |
                        ((uint32_t)arp[17] << 24);
    if (target_mac)
        memcpy(target_mac, arp + 18, FL_NET_ETH_ADDR_LEN);
    if (target_ip_be)
        *target_ip_be = (uint32_t)arp[24] | ((uint32_t)arp[25] << 8) | ((uint32_t)arp[26] << 16) |
                        ((uint32_t)arp[27] << 24);
    return 1;
}

fl_result_t fl_net_arp_input(const uint8_t *frame, size_t len, uint32_t local_ip_be,
                             const uint8_t local_mac[FL_NET_ETH_ADDR_LEN], uint8_t *reply_frame,
                             size_t reply_cap, size_t *reply_len) {
    uint16_t op = 0;
    uint32_t sender_ip = 0;
    uint32_t target_ip = 0;
    uint8_t sender_mac[FL_NET_ETH_ADDR_LEN];
    uint8_t target_mac[FL_NET_ETH_ADDR_LEN];

    if (reply_len)
        *reply_len = 0;

    if (!fl_net_arp_parse_eth(frame, len, &op, &sender_ip, sender_mac, &target_ip, target_mac))
        return FL_RESULT_ERR;

    if (op == FL_NET_ARP_OP_REPLY)
        return fl_net_arp_cache_insert(sender_ip, sender_mac);

    if (op != FL_NET_ARP_OP_REQUEST)
        return FL_RESULT_ERR;

    if (target_ip != local_ip_be)
        return FL_RESULT_OK;

    (void)fl_net_arp_cache_insert(sender_ip, sender_mac);

    if (!reply_frame || !reply_len || !local_mac)
        return FL_RESULT_OK;

    *reply_len = fl_net_arp_build_reply(reply_frame, reply_cap, local_mac, local_ip_be, sender_mac,
                                        sender_ip);
    return (*reply_len > 0) ? FL_RESULT_OK : FL_RESULT_ERR;
}

static fl_result_t arp_resolve_loopback(uint32_t target_ip_be, uint8_t mac_out[FL_NET_ETH_ADDR_LEN]) {
    if (!fl_net_ipv4_is_loopback(target_ip_be))
        return FL_RESULT_ERR;
    fl_net_loopback_mac_peer(mac_out);
    return fl_net_arp_cache_insert(target_ip_be, mac_out);
}

static fl_result_t arp_exchange_wire(fl_net_driver_t *drv, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                                     uint32_t src_ip_be, uint32_t target_ip_be,
                                     uint8_t mac_out[FL_NET_ETH_ADDR_LEN], unsigned timeout_ms) {
    uint8_t req_frame[FL_NET_ETH_FRAME_HDR_LEN + FL_NET_ARP_PKT_LEN];
    uint8_t rx_frame[FL_NET_WIRE_FRAME_BUF_MAX];
    size_t req_len;
    fl_net_frame_view_t view;
    fl_net_frame_mut_t mut;
    unsigned elapsed = 0;
    const unsigned step_ms = 50u;

    req_len = fl_net_arp_build_request(req_frame, sizeof(req_frame), src_mac, src_ip_be, target_ip_be);
    if (req_len == 0)
        return FL_RESULT_ERR;

    view.data = req_frame;
    view.len = req_len;
    if (fl_net_netdev_send(drv, &view) != FL_RESULT_OK)
        return FL_RESULT_ERR;

    while (elapsed < timeout_ms) {
        unsigned wait = step_ms;
        fl_result_t rc;
        uint16_t op;
        uint32_t sip;
        uint8_t smac[FL_NET_ETH_ADDR_LEN];

        if (timeout_ms - elapsed < wait)
            wait = timeout_ms - elapsed;

        mut.data = rx_frame;
        mut.cap = sizeof(rx_frame);
        mut.len = 0;
        rc = fl_net_netdev_recv(drv, &mut, wait);
        elapsed += wait;
        if (rc != FL_RESULT_OK)
            continue;

        if (!fl_net_arp_parse_eth(rx_frame, mut.len, &op, &sip, smac, NULL, NULL))
            continue;
        if (op != FL_NET_ARP_OP_REPLY || sip != target_ip_be)
            continue;

        memcpy(mac_out, smac, FL_NET_ETH_ADDR_LEN);
        (void)fl_net_arp_cache_insert(target_ip_be, mac_out);
        return FL_RESULT_OK;
    }

    return FL_RESULT_TIMEDOUT;
}

fl_result_t fl_net_arp_resolve(fl_net_driver_t *drv, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                               uint32_t src_ip_be, uint32_t target_ip_be,
                               uint8_t mac_out[FL_NET_ETH_ADDR_LEN], unsigned timeout_ms) {
    if (!drv || !src_mac || !mac_out)
        return FL_RESULT_INVAL;

    if (fl_net_arp_cache_lookup(target_ip_be, mac_out))
        return FL_RESULT_OK;

    if (drv == fl_net_netdev_loopback())
        return arp_resolve_loopback(target_ip_be, mac_out);

    return arp_exchange_wire(drv, src_mac, src_ip_be, target_ip_be, mac_out, timeout_ms);
}
