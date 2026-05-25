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

#endif /* NET_ASM_H */
