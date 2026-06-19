#ifndef NET_NDP_H
#define NET_NDP_H

#include "contract_p3_ipv6.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

void fl_net_ndp_init(void);

fl_result_t fl_net_ndp_cache_insert(const uint8_t ip6[FL_NET_IPV6_ADDR_LEN],
                                    const uint8_t mac[FL_NET_ETH_ADDR_LEN]);

fl_result_t fl_net_ndp_cache_lookup(const uint8_t ip6[FL_NET_IPV6_ADDR_LEN],
                                    uint8_t mac_out[FL_NET_ETH_ADDR_LEN]);

size_t fl_net_ndp_build_neighbor_solicit(uint8_t *icmp6, size_t cap, const uint8_t target[16],
                                        const uint8_t src_mac[6]);

size_t fl_net_ndp_build_neighbor_advert(uint8_t *icmp6, size_t cap, const uint8_t target[16],
                                      const uint8_t tgt_mac[6], int solicited, int override);

fl_result_t fl_net_ndp_input(const uint8_t *icmp6, size_t icmp_len, const uint8_t *src6,
                             const uint8_t *dst6, const uint8_t *sender_mac,
                             uint8_t *icmp_reply, size_t reply_cap, size_t *reply_len);

#endif /* NET_NDP_H */
