#ifndef NET_MACVLAN_H
#define NET_MACVLAN_H

#include "contract_result.h"

#include <stdint.h>

#define FL_NET_MACVLAN_NAME "fl0"
#define FL_NET_MACVLAN_IFNAMSIZ 16

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a macvlan interface <name> in bridge mode off the best detected LAN
 * parent (first non-loopback interface with a 192.168/10/172.16 address).
 * If the interface already exists it is brought up and FL_RESULT_OK is returned.
 * parent_out (may be NULL) receives the parent interface name on creation.
 * Returns FL_RESULT_ACCES when elevated privileges are required.
 */
fl_result_t fl_net_macvlan_create(const char *name,
                                  char parent_out[FL_NET_MACVLAN_IFNAMSIZ]);

/** Remove a macvlan interface and clear any cached lease. */
void fl_net_macvlan_destroy(const char *name);

/** Read the hardware (MAC) address of any named interface. */
fl_result_t fl_net_macvlan_hwaddr(const char *ifname, uint8_t mac_out[6]);

/**
 * Obtain a DHCP lease on <ifname> using a standalone AF_PACKET raw-socket
 * DHCP client that does not require a pre-configured IP address.  Sends
 * DISCOVER from mac_in, waits for OFFER, sends REQUEST, waits for ACK.
 * On success ip_be/mask_be/gw_be (all network byte order) are filled and
 * the IP is configured on the interface via `ip addr add`.
 */
fl_result_t fl_net_macvlan_dhcp_lease(const char *ifname,
                                       const uint8_t mac_in[6],
                                       uint32_t *ip_be,
                                       uint32_t *mask_be,
                                       uint32_t *gw_be,
                                       unsigned timeout_ms);

/**
 * Return 1 (and fill optional out-params) if fl0 holds a valid DHCP lease.
 * ip_be and mask_be are in network byte order; prefix_len is the CIDR /N.
 */
int fl_net_macvlan_get_registered(uint32_t *ip_be,
                                   uint32_t *mask_be,
                                   uint8_t  *prefix_len);

#ifdef __cplusplus
}
#endif

#endif /* NET_MACVLAN_H */
