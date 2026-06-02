#ifndef NET_WIRE_H
#define NET_WIRE_H

/**
 * **P3 wire** implementation — **contract_p3_wire.h** vocabulary.
 * L2 frame views, MTU bounds, and DIX Ethernet helpers used by **P3-1** / **P3-2** / **P3-3**.
 */
#include "contract_p3_wire.h"
#include "contract_result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FL_NET_WIRE_IMPL_DEFINED 1
#define FL_NET_WIRE_EXPECT_REV FL_CONTRACT_P3_WIRE_REV

/** DIX header (dst + src + ethertype); FCS is out-of-band per **P3-1** driver policy. */
#define FL_NET_ETH_HDR_LEN FL_NET_ETH_FRAME_HDR_LEN

/** Max host buffer for one TX/RX frame at default MTU (header + payload, no FCS). */
#define FL_NET_WIRE_FRAME_BUF_MAX (FL_NET_ETH_FRAME_HDR_LEN + FL_NET_ETH_MTU_DEFAULT)

fl_net_frame_view_t fl_net_frame_view_make(const uint8_t *data, size_t len);
fl_net_frame_mut_t fl_net_frame_mut_make(uint8_t *data, size_t cap);
fl_net_frame_view_t fl_net_frame_view_from_mut(const fl_net_frame_mut_t *mut);

fl_result_t fl_net_wire_check_view(const fl_net_frame_view_t *view, size_t min_len);
fl_result_t fl_net_wire_check_mut(const fl_net_frame_mut_t *mut);
fl_result_t fl_net_wire_check_tx(const fl_net_frame_view_t *frame, unsigned mtu);
fl_result_t fl_net_wire_check_rx_fill(fl_net_frame_mut_t *out, size_t received);

size_t fl_net_wire_frame_max(unsigned mtu);
uint16_t fl_net_wire_ethertype_be16(const uint8_t *frame, size_t len, int *ok);
int fl_net_wire_ethertype_is_ipv4(const uint8_t *frame, size_t len);

void fl_net_wire_mac_copy(fl_eth_mac_t dst, const uint8_t src[FL_NET_ETH_ADDR_LEN]);
int fl_net_wire_mac_equal(const uint8_t a[FL_NET_ETH_ADDR_LEN],
                          const uint8_t b[FL_NET_ETH_ADDR_LEN]);

size_t fl_net_wire_build_eth_ipv4(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                                  const uint8_t src_mac[6], const uint8_t *ipv4,
                                  size_t ipv4_len);

size_t fl_net_wire_build_eth_ipv6(uint8_t *frame, size_t cap, const uint8_t dst_mac[6],
                                  const uint8_t src_mac[6], const uint8_t *ipv6,
                                  size_t ipv6_len);

int fl_net_wire_ethertype_is_ipv6(const uint8_t *frame, size_t len);

int fl_net_wire_parse_eth_ipv6(const uint8_t *frame, size_t len, size_t *ip_off,
                               size_t *ip_len);

int fl_net_wire_parse_eth_ipv4(const uint8_t *frame, size_t len, size_t *ip_off,
                               size_t *ip_len, fl_ipv4_be32_t *dst_addr);

fl_net_frame_view_t fl_net_wire_slice_ipv4_payload(const uint8_t *frame, size_t len,
                                                 size_t *ip_off_out);

void fl_net_loopback_mac_host(uint8_t mac[FL_NET_ETH_ADDR_LEN]);
void fl_net_loopback_mac_peer(uint8_t mac[FL_NET_ETH_ADDR_LEN]);

void fl_net_wire_init(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_WIRE_H */
