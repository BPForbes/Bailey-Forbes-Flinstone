#include "net_checksum.h"

#include "fl/net_asm.h"

#include <stddef.h>
#include <stdint.h>

/** Use ASM Internet checksum when buffer is at least this long (octets). */
#ifndef FL_NET_CHECKSUM_ASM_THRESHOLD
#define FL_NET_CHECKSUM_ASM_THRESHOLD 64u
#endif

/**
 * Add big-endian 16-bit words from **p** into **sum** (Internet checksum accumulator).
 * Odd trailing byte is padded in the high octet of the last 16-bit lane (**RFC 1071**).
 */
static uint32_t checksum16_accum(uint32_t sum, const uint8_t *p, size_t len) {
    if (!p || len == 0)
        return sum;

    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | (uint16_t)p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)p[0] << 8;
    return sum;
}

/** Fold 32-bit accumulator carries into 16 bits. */
static uint32_t checksum16_fold32(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return sum;
}

/** Fold and ones-complement for on-the-wire checksum. */
static uint16_t checksum16_ones_complement(uint32_t sum) {
    return (uint16_t)(~checksum16_fold32(sum));
}

/** Return 1 when folded ones-complement of **sum** is 0 (valid checksummed payload). */
static int checksum16_acc_is_valid(uint32_t sum) {
    return checksum16_ones_complement(sum) == 0;
}

/** Add IPv4 TCP/UDP pseudo-header (12 octets) into **sum**. */
static uint32_t checksum16_accum_ipv4_pseudo(uint32_t sum, uint32_t src_be, uint32_t dst_be,
                                            uint8_t proto, size_t seg_len) {
    uint8_t pseudo[12];

    pseudo[0] = (uint8_t)(src_be & 0xffu);
    pseudo[1] = (uint8_t)((src_be >> 8) & 0xffu);
    pseudo[2] = (uint8_t)((src_be >> 16) & 0xffu);
    pseudo[3] = (uint8_t)((src_be >> 24) & 0xffu);
    pseudo[4] = (uint8_t)(dst_be & 0xffu);
    pseudo[5] = (uint8_t)((dst_be >> 8) & 0xffu);
    pseudo[6] = (uint8_t)((dst_be >> 16) & 0xffu);
    pseudo[7] = (uint8_t)((dst_be >> 24) & 0xffu);
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (uint8_t)((seg_len >> 8) & 0xff);
    pseudo[11] = (uint8_t)(seg_len & 0xff);
    return checksum16_accum(sum, pseudo, sizeof(pseudo));
}

static uint16_t checksum16_from_buffer(const void *data, size_t len) {
    return checksum16_ones_complement(checksum16_accum(0, (const uint8_t *)data, len));
}

uint16_t fl_net_checksum16(const void *data, size_t len) {
    if (!data || len == 0)
        return 0xffffu;

#if defined(FL_NET_ASM_AVAILABLE)
    if (len >= (size_t)FL_NET_CHECKSUM_ASM_THRESHOLD)
        return asm_net_checksum16(data, len);
#endif
    return checksum16_from_buffer(data, len);
}

int fl_net_checksum16_valid(const void *data, size_t len) {
    if (!data || len == 0)
        return 0;
    return checksum16_acc_is_valid(checksum16_accum(0, (const uint8_t *)data, len));
}

uint16_t fl_net_ipv4_checksum(const void *hdr, size_t hdr_len) {
    return fl_net_checksum16(hdr, hdr_len);
}

uint16_t fl_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
                                       const void *seg, size_t seg_len) {
    uint32_t sum;

    sum = checksum16_accum_ipv4_pseudo(0, src_be, dst_be, proto, seg_len);
    if (seg && seg_len > 0)
        sum = checksum16_accum(sum, (const uint8_t *)seg, seg_len);
    return checksum16_ones_complement(sum);
}
