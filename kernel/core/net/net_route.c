#include "net_route.h"

#include "net_ipv4.h"
#include "net_loopback.h"
#include "net_netdev.h"
#include "net_wire.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fl_net_route_entry_t s_routes[FL_NET_ROUTE_TABLE_MAX];
static unsigned s_route_count;

int fl_net_ipv4_prefix_match(uint32_t addr_be, uint32_t net_be, uint8_t prefix_len) {
    unsigned i;

    if (prefix_len == 0)
        return 1;
    if (prefix_len > FL_NET_IPV4_MAX_PREFIX_LEN)
        return 0;

    for (i = 0; i < 4u; i++) {
        unsigned bits;
        uint8_t ab;
        uint8_t nb;
        uint8_t mask;

        if (prefix_len <= i * 8u)
            break;
        bits = prefix_len - i * 8u;
        if (bits > 8u)
            bits = 8u;
        ab = (uint8_t)((addr_be >> (i * 8u)) & 0xffu);
        nb = (uint8_t)((net_be >> (i * 8u)) & 0xffu);
        mask = (uint8_t)(bits == 8u ? 0xffu : (uint8_t)(0xffu << (8u - bits)));
        if ((ab & mask) != (nb & mask))
            return 0;
    }
    return 1;
}

void fl_net_route_init(void) {
    fl_net_route_clear();
    fl_net_route_add_loopback();
}

void fl_net_route_clear(void) {
    memset(s_routes, 0, sizeof(s_routes));
    s_route_count = 0;
}

void fl_net_route_add_loopback(void) {
    uint8_t host_mac[FL_NET_ETH_ADDR_LEN];
    uint32_t loop_net;
    fl_net_route_entry_t existing;

    fl_net_loopback_mac_host(host_mac);
    loop_net = (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET;
    if (fl_net_route_lookup(loop_net, &existing) == FL_RESULT_OK &&
        existing.prefix_len == 8u && existing.drv == fl_net_netdev_loopback())
        return;
    (void)fl_net_route_add(loop_net, 8u, 0, fl_net_netdev_loopback(),
                           (uint32_t)FL_NET_IPV4_LOOPBACK_FIRST_OCTET | (1u << 24), host_mac);
}

fl_result_t fl_net_route_add(uint32_t addr_be, uint8_t prefix_len, uint32_t gw_be,
                             fl_net_driver_t *drv, uint32_t src_ip_be, const uint8_t src_mac[6]) {
    fl_net_route_entry_t *e;

    /* #267: only 0.0.0.0/0 is valid for prefix /0; other nets with /0 are catch-alls in
     * fl_net_ipv4_prefix_match and are rejected (e.g. 1.2.3.4/0). */
    if (prefix_len == 0u && addr_be != 0u)
        return FL_RESULT_INVAL;
    if (!drv || prefix_len > FL_NET_IPV4_MAX_PREFIX_LEN)
        return FL_RESULT_INVAL;
    if (s_route_count >= FL_NET_ROUTE_TABLE_MAX)
        return FL_RESULT_ERR;

    e = &s_routes[s_route_count++];
    e->addr_be = addr_be;
    e->prefix_len = prefix_len;
    e->gw_be = gw_be;
    e->drv = drv;
    e->src_ip_be = src_ip_be;
    e->src_mac_valid = 0;
    if (src_mac) {
        memcpy(e->src_mac, src_mac, 6);
        e->src_mac_valid = 1;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_net_route_lookup(uint32_t dst_be, fl_net_route_entry_t *out) {
    unsigned i;
    fl_net_route_entry_t *best = NULL;
    uint8_t best_prefix = 0;

    if (!out)
        return FL_RESULT_INVAL;

    for (i = 0; i < s_route_count; i++) {
        fl_net_route_entry_t *e = &s_routes[i];
        if (!fl_net_ipv4_prefix_match(dst_be, e->addr_be, e->prefix_len))
            continue;
        if (!best || e->prefix_len > best_prefix) {
            best = e;
            best_prefix = e->prefix_len;
        }
    }

    if (!best)
        return FL_RESULT_NOENT;

    *out = *best;
    return FL_RESULT_OK;
}

uint32_t fl_net_route_next_hop(uint32_t dst_be, const fl_net_route_entry_t *route) {
    if (!route)
        return dst_be;
    if (route->gw_be == 0)
        return dst_be;
    if (fl_net_ipv4_prefix_match(dst_be, route->addr_be, route->prefix_len))
        return dst_be;
    return route->gw_be;
}

static int route_parse_prefix(const char *prefix_s, unsigned *out_prefix) {
    char *end = NULL;
    unsigned long v;
    unsigned long max = 32u;

    if (!prefix_s || !prefix_s[0] || !out_prefix)
        return 0;

    errno = 0;
    v = strtoul(prefix_s, &end, 10);
    if (errno == ERANGE || end == prefix_s || (end && *end != '\0'))
        return 0;
    if (v == 0 || v > max)
        return 0;
    *out_prefix = (unsigned)v;
    return 1;
}

fl_result_t fl_net_route_configure_static(fl_net_driver_t *drv, const uint8_t src_mac[6],
                                          const char *addr_s, unsigned prefix_len,
                                          const char *gw_s) {
    uint32_t addr_be = 0;
    uint32_t gw_be = 0;
    uint32_t net_be;

    if (!drv || !src_mac || !addr_s || !addr_s[0] || !gw_s || !gw_s[0])
        return FL_RESULT_INVAL;
    if (!fl_net_ipv4_parse_literal(addr_s, &addr_be))
        return FL_RESULT_INVAL;
    if (!fl_net_ipv4_parse_literal(gw_s, &gw_be))
        return FL_RESULT_INVAL;
    if (prefix_len == 0u || prefix_len > 32u)
        return FL_RESULT_INVAL;

    net_be = fl_net_ipv4_network_addr(addr_be, (uint8_t)prefix_len);
    return fl_net_route_add(net_be, (uint8_t)prefix_len, gw_be, drv, addr_be, src_mac);
}

#if defined(__linux__)
fl_result_t fl_net_route_configure_tap(fl_net_driver_t *tap_drv, const uint8_t tap_mac[6],
                                       const char *tap_ifname) {
    const char *addr_s;
    const char *prefix_s;
    const char *gw_s;
    unsigned prefix = 24;

    (void)tap_ifname;

    if (!tap_drv || !tap_mac)
        return FL_RESULT_INVAL;

    addr_s = getenv("FL_NET_TAP_IPV4");
    if (!addr_s || !addr_s[0])
        addr_s = "10.0.2.15";
    prefix_s = getenv("FL_NET_TAP_PREFIX");
    if (prefix_s && prefix_s[0]) {
        unsigned parsed = 0;

        if (!route_parse_prefix(prefix_s, &parsed))
            return FL_RESULT_INVAL;
        prefix = parsed;
    }
    gw_s = getenv("FL_NET_TAP_GW");
    if (!gw_s || !gw_s[0])
        gw_s = "10.0.2.2";

    return fl_net_route_configure_static(tap_drv, tap_mac, addr_s, prefix, gw_s);
}
#endif
