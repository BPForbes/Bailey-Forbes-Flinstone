#include "net_iface.h"

#include "net_endian.h"
#include "net_ipv4.h"
#include "net_ipv6.h"
#include "net_loopback.h"
#include "net_macvlan.h"
#include "net_netdev.h"
#include "net_route.h"
#include "net_wifi_netdev.h"
#include "net_wifi_host_linux.h"
#include "net_wifi_station.h"

#include <stdio.h>
#include <string.h>

#define FL_NET_IFACE_TABLE_MAX 16u

static fl_net_iface_entry_t s_ifaces[FL_NET_IFACE_TABLE_MAX];
static unsigned s_iface_count;
static unsigned s_next_ifindex = 1u;

static fl_net_iface_entry_t *iface_find_driver(const fl_net_driver_t *drv) {
    unsigned i;
    for (i = 0; i < s_iface_count; i++) {
        if (s_ifaces[i].drv == drv)
            return &s_ifaces[i];
    }
    return NULL;
}

static fl_net_iface_entry_t *iface_alloc(const char *name, fl_net_driver_t *drv,
                                         unsigned flags) {
    fl_net_iface_entry_t *e;
    if (s_iface_count >= FL_NET_IFACE_TABLE_MAX || !name || !drv)
        return NULL;
    e = &s_ifaces[s_iface_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1u);
    e->ifindex = s_next_ifindex++;
    e->flags = flags | FL_NET_IFF_UP;
    e->drv = drv;
    return e;
}


static void iface_add_route6_row(const fl_net_route6_entry_t *route) {
    fl_net_iface_entry_t *e;
    const char *name;
    int empty6 = 1;
    unsigned i;

    if (!route || !route->drv)
        return;
    for (i = 0; i < 16u; i++) {
        if (route->src6[i] != 0u) {
            empty6 = 0;
            break;
        }
    }
    if (empty6)
        return;

    e = iface_find_driver(route->drv);
    if (!e) {
        if (route->drv == fl_net_netdev_loopback()) {
            name = "lo";
            e = iface_alloc(name, route->drv, FL_NET_IFF_LOOPBACK);
        } else if (fl_net_netdev_tap() && route->drv == fl_net_netdev_tap()) {
            name = fl_net_netdev_tap_ifname();
            if (!name || !name[0])
                name = "tap0";
            e = iface_alloc(name, route->drv, 0u);
        } else if (fl_net_wifi_netdev_driver() &&
                   route->drv == fl_net_wifi_netdev_driver()) {
            name = fl_net_wifi_netdev_iface();
            e = iface_alloc(name, route->drv, 0u);
        } else {
            char gen[FL_NET_IFACE_NAME_MAX];
            snprintf(gen, sizeof(gen), "eth%u", s_iface_count);
            e = iface_alloc(gen, route->drv, 0u);
        }
    }
    if (!e)
        return;
    memcpy(e->addr6, route->src6, 16);
    e->prefix6_len = route->prefix_len;
    e->has_ipv6 = 1;
}

static void iface_add_route_row(const fl_net_route_entry_t *route) {
    fl_net_iface_entry_t *e;
    const char *name;

    if (!route || !route->drv || route->src_ip_be == 0u)
        return;

    e = iface_find_driver(route->drv);
    if (!e) {
        if (route->drv == fl_net_netdev_loopback()) {
            name = "lo";
            e = iface_alloc(name, route->drv, FL_NET_IFF_LOOPBACK);
        } else if (fl_net_netdev_tap() && route->drv == fl_net_netdev_tap()) {
            name = fl_net_netdev_tap_ifname();
            if (!name || !name[0])
                name = "tap0";
            e = iface_alloc(name, route->drv, 0u);
        } else if (fl_net_wifi_netdev_driver() &&
                   route->drv == fl_net_wifi_netdev_driver()) {
            name = fl_net_wifi_netdev_iface();
            e = iface_alloc(name, route->drv, 0u);
        } else {
            char gen[FL_NET_IFACE_NAME_MAX];
            snprintf(gen, sizeof(gen), "eth%u", s_iface_count);
            e = iface_alloc(gen, route->drv, 0u);
        }
    }
    if (!e)
        return;
    e->addr_be = route->src_ip_be;
    if (route->prefix_len > 0u)
        e->prefix_len = route->prefix_len;
}

static void iface_add_host_wifi_row(void) {
    fl_net_iface_entry_t *e;
    uint32_t addr_be = 0u;
    uint8_t prefix = 24u;
    const char *name;

    if (!fl_net_wifi_station_host_backend())
        return;
    if (fl_net_wifi_host_linux_ipv4_route(&addr_be, &prefix, NULL) != FL_RESULT_OK ||
        addr_be == 0u)
        return;
    name = fl_net_wifi_host_linux_iface();
    if (!name || !name[0])
        name = "wlan0";
    e = iface_alloc(name, fl_net_netdev_loopback(), 0u);
    if (!e)
        return;
    e->addr_be = addr_be;
    if (prefix > 0u)
        e->prefix_len = prefix;
}

void fl_net_iface_refresh(void) {
    fl_net_route_entry_t routes[FL_NET_ROUTE_TABLE_MAX];
    fl_net_route6_entry_t routes6[FL_NET_ROUTE_TABLE_MAX];
    unsigned n;
    unsigned n6;
    unsigned i;

    s_iface_count = 0u;
    s_next_ifindex = 1u;
    memset(s_ifaces, 0, sizeof(s_ifaces));

    n = fl_net_route_snapshot(routes, FL_NET_ROUTE_TABLE_MAX);
    for (i = 0; i < n; i++)
        iface_add_route_row(&routes[i]);

    n6 = fl_net_route_snapshot6(routes6, FL_NET_ROUTE_TABLE_MAX);
    for (i = 0; i < n6; i++)
        iface_add_route6_row(&routes6[i]);

    iface_add_host_wifi_row();

    /* fl0 macvlan with DHCP lease — Flinstone's own LAN identity. */
    {
        uint32_t fl0_ip = 0u;
        uint8_t  fl0_prefix = 24u;
        if (fl_net_macvlan_get_registered(&fl0_ip, NULL, &fl0_prefix) && fl0_ip != 0u) {
            fl_net_iface_entry_t *e = iface_alloc("fl0", fl_net_netdev_loopback(), 0u);
            if (e) {
                e->addr_be   = fl0_ip;
                e->prefix_len = fl0_prefix;
            }
        }
    }

    if (s_iface_count == 0u) {
        fl_net_route_entry_t loop;
        if (fl_net_route_lookup(fl_net_htonl(0x7F000001u), &loop) == FL_RESULT_OK)
            iface_add_route_row(&loop);
    }
}

unsigned fl_net_iface_list(fl_net_iface_entry_t *out, unsigned cap) {
    unsigned i;
    if (s_iface_count == 0u)
        fl_net_iface_refresh();
    if (!out || cap == 0u)
        return s_iface_count;
    for (i = 0; i < s_iface_count && i < cap; i++)
        out[i] = s_ifaces[i];
    return (i < s_iface_count) ? cap : s_iface_count;
}

int fl_net_iface_suggest_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len) {
    fl_net_iface_entry_t entries[FL_NET_IFACE_TABLE_MAX];
    unsigned count;
    unsigned i;
    uint32_t pick = 0u;

    /*
     * fl0 macvlan (Flinstone's own LAN identity): highest priority when a
     * DHCP lease has been obtained.
     */
    {
        uint32_t fl0 = 0u;
        if (fl_net_macvlan_get_registered(&fl0, NULL, NULL) && fl0 != 0u) {
            if (addr_be_out) *addr_be_out = fl0;
            if (buf && buf_len > 0u)
                fl_net_ipv4_format_addr(fl0, buf, buf_len);
            return 1;
        }
    }

    /*
     * Host Wi-Fi backend (wpa_cli / FlinstonePowershell): bindable IPv4 from the
     * host stack, not the in-tree wlan-lab netdev.
     */
    if (fl_net_wifi_station_host_backend()) {
        uint32_t host_be = 0u;
        if (fl_net_wifi_host_linux_ipv4(&host_be, buf, buf_len) == FL_RESULT_OK &&
            host_be != 0u) {
            if (addr_be_out)
                *addr_be_out = host_be;
            return 1;
        }
    }

    /*
     * In-tree WiFi netdev (lab/driver) is the default peer hint.
     */
    if (fl_net_wifi_netdev_is_up()) {
        uint32_t nd = 0u;
        if (fl_net_wifi_netdev_ipv4(&nd) == FL_RESULT_OK && nd != 0u) {
            if (addr_be_out)
                *addr_be_out = nd;
            if (buf && buf_len > 0u)
                fl_net_ipv4_format_addr(nd, buf, buf_len);
            return 1;
        }
    }

    count = fl_net_iface_list(entries, FL_NET_IFACE_TABLE_MAX);
    for (i = 0; i < count; i++) {
        if (!(entries[i].flags & FL_NET_IFF_UP))
            continue;
        if (entries[i].flags & FL_NET_IFF_LOOPBACK)
            continue;
        if ((entries[i].addr_be & 0xffu) == 169u &&
            ((entries[i].addr_be >> 8) & 0xffu) == 254u)
            continue;
        pick = entries[i].addr_be;
        break;
    }
    if (pick == 0u)
        return 0;
    if (addr_be_out)
        *addr_be_out = pick;
    if (buf && buf_len > 0u)
        fl_net_ipv4_format_addr(pick, buf, buf_len);
    return 1;
}

const char *fl_net_iface_name_for_driver(const fl_net_driver_t *drv) {
    fl_net_iface_entry_t *e;
    if (s_iface_count == 0u)
        fl_net_iface_refresh();
    e = iface_find_driver(drv);
    return e ? e->name : "";
}
