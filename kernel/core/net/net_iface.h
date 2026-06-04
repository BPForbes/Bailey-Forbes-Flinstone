#ifndef NET_IFACE_H
#define NET_IFACE_H

#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

/** Linux-style interface name length (IFNAMSIZ). */
#define FL_NET_IFACE_NAME_MAX 16u

/** Interface flags (Linux IFF_* subset, in-tree only). */
#define FL_NET_IFF_UP        0x0001u
#define FL_NET_IFF_LOOPBACK  0x0004u

/** One registered IPv4 network interface (rtnetlink/netdev view, in-tree). */
typedef struct fl_net_iface_entry {
    char name[FL_NET_IFACE_NAME_MAX];
    unsigned ifindex;
    unsigned flags;
    fl_net_driver_t *drv;
    uint32_t addr_be;
    uint8_t prefix_len;
} fl_net_iface_entry_t;

/** Rebuild the interface table from the in-tree route FIB and netdev registry. */
void fl_net_iface_refresh(void);

/** List IPv4 interfaces; returns row count (0 when none). */
unsigned fl_net_iface_list(fl_net_iface_entry_t *out, unsigned cap);

/** Suggested non-loopback IPv4 for LAN join hints; 0 when none. */
int fl_net_iface_suggest_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len);

const char *fl_net_iface_name_for_driver(const fl_net_driver_t *drv);

#endif /* NET_IFACE_H */
