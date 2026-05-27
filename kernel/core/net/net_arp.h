#ifndef NET_ARP_H
#define NET_ARP_H

#include "contract_p3_arp.h"
#include "contract_result.h"
#include "fl/driver/net.h"

#include <stddef.h>
#include <stdint.h>

/** ARP message body length for Ethernet/IPv4 (**RFC 826**). */
#define FL_NET_ARP_PKT_LEN 28u

#define FL_NET_ARP_OP_REQUEST 1u
#define FL_NET_ARP_OP_REPLY 2u

#ifndef FL_NET_ARP_CACHE_MAX
#define FL_NET_ARP_CACHE_MAX 32u
#endif

/** Fixed stride for **asm_net_arp_cache_*** and C **fl_net_arp_cache_entry_t**. */
#define FL_NET_ARP_CACHE_ENTRY_STRIDE 16u
#define FL_NET_ARP_CACHE_OFF_IP_BE 0u
#define FL_NET_ARP_CACHE_OFF_MAC 4u
#define FL_NET_ARP_CACHE_OFF_AGE 12u

void fl_net_arp_init(void);
void fl_net_arp_clear(void);

fl_result_t fl_net_arp_cache_insert(uint32_t ipv4_be, const uint8_t mac[FL_NET_ETH_ADDR_LEN]);
int fl_net_arp_cache_lookup(uint32_t ipv4_be, uint8_t mac_out[FL_NET_ETH_ADDR_LEN]);

size_t fl_net_arp_build_request(uint8_t *frame, size_t cap, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                                uint32_t src_ip_be, uint32_t target_ip_be);

size_t fl_net_arp_build_reply(uint8_t *frame, size_t cap, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                              uint32_t src_ip_be, const uint8_t dst_mac[FL_NET_ETH_ADDR_LEN],
                              uint32_t dst_ip_be);

int fl_net_arp_parse_eth(const uint8_t *frame, size_t len, uint16_t *out_op,
                         uint32_t *sender_ip_be, uint8_t sender_mac[FL_NET_ETH_ADDR_LEN],
                         uint32_t *target_ip_be, uint8_t target_mac[FL_NET_ETH_ADDR_LEN]);

/**
 * Handle an inbound ARP frame. On **request** for **local_ip_be**, may write a reply
 * Ethernet frame into **reply_frame** (optional).
 */
fl_result_t fl_net_arp_input(const uint8_t *frame, size_t len, uint32_t local_ip_be,
                             const uint8_t local_mac[FL_NET_ETH_ADDR_LEN], uint8_t *reply_frame,
                             size_t reply_cap, size_t *reply_len);

/**
 * Resolve **target_ip_be** to a MAC on **drv**. Loopback uses synthetic MACs without wire ARP.
 */
fl_result_t fl_net_arp_resolve(fl_net_driver_t *drv, const uint8_t src_mac[FL_NET_ETH_ADDR_LEN],
                               uint32_t src_ip_be, uint32_t target_ip_be,
                               uint8_t mac_out[FL_NET_ETH_ADDR_LEN], unsigned timeout_ms);

/**
 * Evict cache entries older than **max_age_ticks** (tick delta from **s_arp_tick**).
 * **P3-14** background sweep; returns count removed.
 */
unsigned fl_net_arp_cache_sweep(unsigned max_age_ticks);

#endif /* NET_ARP_H */
