#ifndef NET_ASM_H
#define NET_ASM_H

#include <stddef.h>
#include <stdint.h>

/**
 * P3 networking ASM (x86-64): Internet checksum and port byte-swap.
 * See docs/P3_NETWORKING.md and arch/x86_64/gas/net_asm.s.
 */
uint16_t asm_net_checksum16(const void *data, size_t len);
uint16_t asm_net_htons_be16(uint16_t host);

size_t asm_net_tcp_build_syn(uint8_t *buf, size_t cap, uint16_t sport, uint16_t dport,
                              uint32_t seq);
size_t asm_net_tcp_build_rst_ack(const uint8_t *syn, size_t syn_len, uint8_t *reply,
                                 size_t cap);
int asm_net_tcp_read_ports_be(const uint8_t *tcp, size_t len, uint16_t *sport,
                              uint16_t *dport);

#endif /* NET_ASM_H */
