#ifndef NET_CHECKSUM_H
#define NET_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

uint16_t fl_net_checksum16(const void *data, size_t len);
int fl_net_checksum16_valid(const void *data, size_t len);
uint16_t fl_net_ipv4_checksum(const void *hdr, size_t hdr_len);
uint16_t fl_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
                                       const void *seg, size_t seg_len);
int fl_net_udp_checksum_valid(uint32_t src_be, uint32_t dst_be, const uint8_t *udp, size_t len);

#endif /* NET_CHECKSUM_H */
