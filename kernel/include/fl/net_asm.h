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
#endif

#endif /* FL_NET_ASM_H */
