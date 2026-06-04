#include "net_host_iface.h"

#include "net_endian.h"
#include "net_ipv4.h"

#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#define FL_NET_HOST_IFACE_HOSTED 1
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

static uint8_t mask_be_to_prefix(uint32_t mask_be) {
    uint32_t m = fl_net_ntohl(mask_be);
    uint8_t bits = 0u;
    while (m & 0x80000000u) {
        bits++;
        m <<= 1;
    }
    return bits;
}

int fl_net_host_iface_list(fl_net_host_iface_entry_t *out, size_t cap, size_t *count_out) {
#if !defined(FL_NET_HOST_IFACE_HOSTED)
    if (count_out)
        *count_out = 0u;
    (void)out;
    (void)cap;
    return 0;
#else
    struct ifaddrs *ifa = NULL;
    struct ifaddrs *cur;
    size_t n = 0u;

    if (!out || !count_out || cap == 0u)
        return 0;
    *count_out = 0u;
    if (getifaddrs(&ifa) != 0)
        return 0;

    for (cur = ifa; cur != NULL; cur = cur->ifa_next) {
        const struct sockaddr_in *sin;
        fl_net_host_iface_entry_t *e;

        if (!cur->ifa_addr || cur->ifa_addr->sa_family != AF_INET)
            continue;
        if (n >= cap)
            break;
        sin = (const struct sockaddr_in *)cur->ifa_addr;
        e = &out[n];
        memset(e, 0, sizeof(*e));
        if (cur->ifa_name)
            strncpy(e->name, cur->ifa_name, sizeof(e->name) - 1u);
        e->addr_be = sin->sin_addr.s_addr;
        e->is_up = (cur->ifa_flags & IFF_UP) ? 1u : 0u;
        e->is_loopback = (cur->ifa_flags & IFF_LOOPBACK) ? 1u : 0u;
        if (cur->ifa_netmask && cur->ifa_netmask->sa_family == AF_INET) {
            const struct sockaddr_in *mask =
                (const struct sockaddr_in *)cur->ifa_netmask;
            e->prefix_len = mask_be_to_prefix(mask->sin_addr.s_addr);
        }
        n++;
    }
    freeifaddrs(ifa);
    *count_out = n;
    return 1;
#endif
}

int fl_net_host_iface_suggest_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len) {
    fl_net_host_iface_entry_t entries[32];
    size_t count = 0u;
    size_t i;
    uint32_t pick = 0u;

    if (!fl_net_host_iface_list(entries, 32, &count))
        return 0;

    for (i = 0; i < count; i++) {
        if (!entries[i].is_up)
            continue;
        if (entries[i].is_loopback)
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
