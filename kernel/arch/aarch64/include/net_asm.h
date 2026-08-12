#ifndef NET_ASM_H
#define NET_ASM_H

#include <stddef.h>
#include <stdint.h>

/**
 * P3 networking ASM (AArch64): Internet checksum and port byte-swap.
 * See docs/P3_NETWORKING.md and arch/arm/gas/net_asm.s.
 */
uint16_t asm_net_checksum16(const void *data, size_t len);
uint16_t asm_net_htons_be16(uint16_t host);

/**
 * Endian primitives backing fl_net_htons / fl_net_htonl / fl_net_ntohs /
 * fl_net_ntohl in kernel/core/net/net_endian.h. ntohs / ntohl are aliases
 * of htons / htonl on rev-style hardware (AArch64); we declare both so
 * callers can keep their intent obvious at the API boundary. The packet
 * module (net_packet.c, net_wire*.c) uses these for header field writes.
 */
uint16_t asm_net_ntohs_be16(uint16_t net);
uint32_t asm_net_htonl_be32(uint32_t host);
uint32_t asm_net_ntohl_be32(uint32_t net);
uint64_t asm_net_htonll_be64(uint64_t host);
uint64_t asm_net_ntohll_be64(uint64_t net);

/** Store/load host integers as little-endian (LSB first) or big-endian (MSB first) octets. */
void asm_net_store_le16(uint8_t *out, uint16_t host);
uint16_t asm_net_load_le16(const uint8_t *in);
void asm_net_store_be16(uint8_t *out, uint16_t host);
uint16_t asm_net_load_be16(const uint8_t *in);
void asm_net_store_le32(uint8_t *out, uint32_t host);
uint32_t asm_net_load_le32(const uint8_t *in);
void asm_net_store_be32(uint8_t *out, uint32_t host);
uint32_t asm_net_load_be32(const uint8_t *in);

size_t asm_net_tcp_build_syn(uint8_t *buf, size_t cap, uint16_t sport, uint16_t dport,
                              uint32_t seq);
size_t asm_net_tcp_build_rst_ack(const uint8_t *syn, size_t syn_len, uint8_t *reply,
                                 size_t cap);
int asm_net_tcp_read_ports_be(const uint8_t *tcp, size_t len, uint16_t *sport,
                              uint16_t *dport);

size_t asm_net_icmp_echo_request_build(uint8_t *buf, size_t cap, uint16_t id, uint16_t seq,
                                       size_t payload_len);
int asm_net_icmp_echo_reply_match(const uint8_t *buf, size_t len, uint16_t id, uint16_t seq);

void asm_net_pseudo_header_fill12(uint8_t *pseudo, uint32_t src_be, uint32_t dst_be,
                                  uint8_t proto, size_t seg_len);
uint16_t asm_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
                                        const void *seg, size_t seg_len);

void asm_net_dns_query_header_prefix(uint8_t *query, uint16_t txid);

void asm_net_arp_cache_clear(void *table, size_t max_entries, unsigned *count, unsigned *tick);
int asm_net_arp_cache_lookup(const void *table, unsigned count, uint32_t ip_be,
                               uint8_t mac_out[6], unsigned *tick);
int asm_net_arp_cache_insert(void *table, unsigned *count, unsigned max_entries,
                             unsigned *tick, uint32_t ip_be, const uint8_t mac[6]);
void asm_net_arp_cache_evict_oldest(void *table, unsigned *count);

#endif /* NET_ASM_H */
