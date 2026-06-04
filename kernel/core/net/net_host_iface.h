#ifndef NET_HOST_IFACE_H
#define NET_HOST_IFACE_H

#include <stddef.h>
#include <stdint.h>

/** Max interface name length (IFNAMSIZ on Linux). */
#define FL_NET_HOST_IFACE_NAME_MAX 16u

/** One IPv4 address bound to a host network interface. */
typedef struct fl_net_host_iface_entry {
    char name[FL_NET_HOST_IFACE_NAME_MAX];
    uint32_t addr_be;
    uint8_t prefix_len;
    unsigned is_up;
    unsigned is_loopback;
} fl_net_host_iface_entry_t;

/**
 * Enumerate IPv4 addresses on local interfaces (hosted OS only).
 * Returns 0 when getifaddrs is unavailable or fails.
 */
int fl_net_host_iface_list(fl_net_host_iface_entry_t *out, size_t cap, size_t *count_out);

/**
 * Pick a suggested non-loopback IPv4 for `server host` hints, or 0 when none.
 * Writes dotted-quad into `buf` when `buf` and `buf_len` are set.
 */
int fl_net_host_iface_suggest_ipv4(uint32_t *addr_be_out, char *buf, size_t buf_len);

#endif /* NET_HOST_IFACE_H */
