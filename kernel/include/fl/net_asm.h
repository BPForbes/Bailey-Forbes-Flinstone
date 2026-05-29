#ifndef FL_NET_ASM_H
#define FL_NET_ASM_H

#if defined(__aarch64__)
#include "../../arch/aarch64/include/net_asm.h"
#elif defined(__x86_64__)
#include "../../arch/x86_64/include/net_asm.h"
#else
#include <stddef.h>
#include <stdint.h>
uint16_t asm_net_checksum16(const void *data, size_t len);
uint16_t asm_net_htons_be16(uint16_t host);
uint16_t asm_net_ntohs_be16(uint16_t net);
uint32_t asm_net_htonl_be32(uint32_t host);
uint32_t asm_net_ntohl_be32(uint32_t net);
uint64_t asm_net_htonll_be64(uint64_t host);
uint64_t asm_net_ntohll_be64(uint64_t net);
#endif

#endif /* FL_NET_ASM_H */
