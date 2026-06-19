#include "net_ndp.h"

#include "net_ipv6.h"
#include "net_wire.h"

#include <string.h>

typedef struct {
    uint8_t ip6[FL_NET_IPV6_ADDR_LEN];
    uint8_t mac[FL_NET_ETH_ADDR_LEN];
    unsigned in_use;
} fl_net_ndp_slot_t;

static fl_net_ndp_slot_t s_ndp[FL_NET_NDP_CACHE_MAX];

void fl_net_ndp_init(void) {
    memset(s_ndp, 0, sizeof(s_ndp));
}

fl_result_t fl_net_ndp_cache_insert(const uint8_t ip6[FL_NET_IPV6_ADDR_LEN],
                                      const uint8_t mac[FL_NET_ETH_ADDR_LEN]) {
    unsigned i;

    if (!ip6 || !mac)
        return FL_RESULT_INVAL;

    for (i = 0; i < FL_NET_NDP_CACHE_MAX; i++) {
        if (s_ndp[i].in_use && memcmp(s_ndp[i].ip6, ip6, FL_NET_IPV6_ADDR_LEN) == 0) {
            memcpy(s_ndp[i].mac, mac, FL_NET_ETH_ADDR_LEN);
            return FL_RESULT_OK;
        }
    }
    for (i = 0; i < FL_NET_NDP_CACHE_MAX; i++) {
        if (!s_ndp[i].in_use) {
            memcpy(s_ndp[i].ip6, ip6, FL_NET_IPV6_ADDR_LEN);
            memcpy(s_ndp[i].mac, mac, FL_NET_ETH_ADDR_LEN);
            s_ndp[i].in_use = 1u;
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_ERR;
}

fl_result_t fl_net_ndp_cache_lookup(const uint8_t ip6[FL_NET_IPV6_ADDR_LEN],
                                    uint8_t mac_out[FL_NET_ETH_ADDR_LEN]) {
    unsigned i;

    if (!ip6 || !mac_out)
        return FL_RESULT_INVAL;
    for (i = 0; i < FL_NET_NDP_CACHE_MAX; i++) {
        if (s_ndp[i].in_use && memcmp(s_ndp[i].ip6, ip6, FL_NET_IPV6_ADDR_LEN) == 0) {
            memcpy(mac_out, s_ndp[i].mac, FL_NET_ETH_ADDR_LEN);
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}

size_t fl_net_ndp_build_neighbor_solicit(uint8_t *icmp6, size_t cap, const uint8_t target[16],
                                        const uint8_t src_mac[6]) {
    size_t need = 32u;
    uint8_t src6[16];
    uint8_t dst6[16];
    uint16_t csum;

    if (!icmp6 || !target || !src_mac || cap < need)
        return 0;

    memset(src6, 0, sizeof(src6));
    memset(dst6, 0, sizeof(dst6));
    dst6[0] = 0xffu;
    dst6[1] = 0x02u;
    dst6[11] = 0x01u;
    dst6[12] = 0xffu;
    memcpy(&dst6[13], target + 13, 3);

    icmp6[0] = (uint8_t)FL_NET_ICMPV6_TYPE_NS;
    icmp6[1] = 0;
    icmp6[2] = 0;
    icmp6[3] = 0;
    icmp6[4] = 0;
    icmp6[5] = 0;
    icmp6[6] = 0;
    icmp6[7] = 0;
    memcpy(icmp6 + 8, target, 16);
    icmp6[24] = 1; /* source link-layer */
    icmp6[25] = 1;
    memcpy(icmp6 + 26, src_mac, 6);

    csum = fl_net_ipv6_icmp6_checksum(src6, dst6, icmp6, need);
    icmp6[2] = (uint8_t)(csum >> 8);
    icmp6[3] = (uint8_t)(csum & 0xff);
    return need;
}

size_t fl_net_ndp_build_neighbor_advert(uint8_t *icmp6, size_t cap, const uint8_t target[16],
                                        const uint8_t tgt_mac[6], int solicited, int override) {
    size_t need = 32u;
    uint8_t src6[16];
    uint8_t dst6[16];
    uint16_t csum;
    uint8_t flags = 0;

    if (!icmp6 || !target || !tgt_mac || cap < need)
        return 0;

    memcpy(src6, target, 16);
    memset(dst6, 0, sizeof(dst6));
    dst6[0] = 0xffu;
    dst6[1] = 0x02u;
    dst6[13] = 0x01u;
    dst6[15] = 0xffu;

    if (solicited)
        flags |= 0x40u;
    if (override)
        flags |= 0x20u;

    icmp6[0] = (uint8_t)FL_NET_ICMPV6_TYPE_NA;
    icmp6[1] = 0;
    icmp6[2] = 0;
    icmp6[3] = 0;
    icmp6[4] = flags;
    icmp6[5] = 0;
    icmp6[6] = 0;
    icmp6[7] = 0;
    memcpy(icmp6 + 8, target, 16);
    icmp6[24] = 2; /* target link-layer */
    icmp6[25] = 1;
    memcpy(icmp6 + 26, tgt_mac, 6);

    csum = fl_net_ipv6_icmp6_checksum(src6, dst6, icmp6, need);
    icmp6[2] = (uint8_t)(csum >> 8);
    icmp6[3] = (uint8_t)(csum & 0xff);
    return need;
}

fl_result_t fl_net_ndp_input(const uint8_t *icmp6, size_t icmp_len, const uint8_t *src6,
                             const uint8_t *dst6, const uint8_t *sender_mac,
                             uint8_t *icmp_reply, size_t reply_cap, size_t *reply_len) {
    const uint8_t *target;
    uint8_t host_mac[6];
    size_t built;

    (void)dst6;

    if (!icmp6 || icmp_len < 24u || !src6 || !sender_mac || !icmp_reply || !reply_len)
        return FL_RESULT_INVAL;

    if (icmp6[0] != (uint8_t)FL_NET_ICMPV6_TYPE_NS)
        return FL_RESULT_TIMEDOUT;

    target = icmp6 + 8;
    if (!fl_net_ipv6_is_loopback(target))
        return FL_RESULT_TIMEDOUT;

    (void)fl_net_ndp_cache_insert(src6, sender_mac);

    fl_net_loopback_mac_host(host_mac);
    built = fl_net_ndp_build_neighbor_advert(icmp_reply, reply_cap, target, host_mac, 1, 1);
    if (built == 0)
        return FL_RESULT_ERR;
    *reply_len = built;
    return FL_RESULT_OK;
}
