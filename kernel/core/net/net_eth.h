#ifndef NET_ETH_H
#define NET_ETH_H

#include <stddef.h>
#include <stdint.h>

#define FL_NET_ETH_HDR_LEN 14u

/** Locally administered MAC for software loopback TX (P3-2). */
#define FL_NET_LOOPBACK_MAC_HOST_0 0x02u
#define FL_NET_LOOPBACK_MAC_HOST_1 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_2 0x5eu
#define FL_NET_LOOPBACK_MAC_HOST_3 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_4 0x00u
#define FL_NET_LOOPBACK_MAC_HOST_5 0x01u

/** Peer MAC used on synthesized loopback replies. */
#define FL_NET_LOOPBACK_MAC_PEER_5 0x02u

/**
 * Build **IEEE 802.3** frame with **IPv4** ethertype. **ipv4** is the full IPv4 datagram.
 * Returns frame length or **0** on overflow.
 */
size_t fl_net_eth_build_ipv4(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                             const uint8_t src_mac[6], const uint8_t *ipv4, size_t ipv4_len);

/**
 * Parse **802.3** frame; on **IPv4** ethertype sets **ip_off** / **ip_len** and **dst_be**
 * (IPv4 destination in **sin_addr** byte order). Returns **1** on success.
 */
int fl_net_eth_parse_ipv4(const uint8_t *frame, size_t len, size_t *ip_off, size_t *ip_len,
                          uint32_t *dst_be);

void fl_net_loopback_mac_host(uint8_t mac[6]);
void fl_net_loopback_mac_peer(uint8_t mac[6]);

#endif /* NET_ETH_H */
