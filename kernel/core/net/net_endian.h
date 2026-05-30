/*
 * net_endian.h — byte-order helpers for P3 wire framing.
 *
 * Internal architecture, not libc. Two implementations stack:
 *
 *   1. ASM-backed (preferred): when `FL_NET_ASM_AVAILABLE` is defined the
 *      helpers route through `asm_net_htons_be16` / `asm_net_htonl_be32`
 *      / `asm_net_ntohs_be16` / `asm_net_ntohl_be32` in
 *      arch/x86_64/{gas,nasm}/net_asm.* and arch/arm/gas/net_asm.s, which
 *      compile to a single `bswap` (x86) / `rev` (AArch64) instruction.
 *      That's the "endian-extended packets" path the packet module
 *      (net_packet.c, net_wire*.c) and the P3-14 net background MLQ
 *      (kernel/core/sched/workqueue + priority_queue.h) call into when
 *      packets are framed for send.
 *
 *   2. Pure C fallback (K/B without ASM, or hosts where the project is
 *      compiled without -DFL_NET_ASM_AVAILABLE): in-tree bit-shift forms
 *      that have zero libc dependency. Both forms have identical
 *      observable behaviour, so callers always see the same `fl_net_*`
 *      surface regardless of which backend is selected at build time.
 *
 * Why memcpy and not bit-shifts on `peer_ip_be`:
 *   `peer_ip_be` is a uint32_t whose memory bytes are already in network
 *   order (filled from POSIX `s_addr` inside the hosted shim). Reading it
 *   as a numeric value and then shifting (>> 8, >> 16, ...) yields LE-only
 *   behaviour: on a BE host the bytes come out reversed. memcpy of the
 *   storage preserves wire order on every host.
 *
 * Tracked by issue #284 (endian round-trip bug + helper consolidation).
 */
#ifndef FL_NET_ENDIAN_H
#define FL_NET_ENDIAN_H

#include <stdint.h>
#include <string.h>

#if defined(FL_NET_ASM_AVAILABLE)
#include "fl/net_asm.h"
#endif

/* Compile-time endianness probe used by the non-ASM fallbacks. `htons` /
 * `htonl` etc. are value transformations: on a BE host the host integer
 * already has the network byte layout, so the helpers must be a no-op
 * there; on LE they must byte-swap. (#284) */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    defined(__ORDER_BIG_ENDIAN__)
#  define FL_NET_HOST_IS_LE (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#  define FL_NET_HOST_IS_BE (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(__LITTLE_ENDIAN__)
#  define FL_NET_HOST_IS_LE 1
#  define FL_NET_HOST_IS_BE 0
#elif defined(__BIG_ENDIAN__)
#  define FL_NET_HOST_IS_LE 0
#  define FL_NET_HOST_IS_BE 1
#else
   /* All currently-supported targets (x86_64 H/K/B, AArch64 lab) are LE.
    * Default to LE so the C fallback keeps working when the compiler
    * doesn't expose the standard endian macros; if you port to a BE
    * target add the case here. */
#  define FL_NET_HOST_IS_LE 1
#  define FL_NET_HOST_IS_BE 0
#endif

/* Host -> network 16-bit. */
static inline uint16_t fl_net_htons(uint16_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_htons_be16(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return (uint16_t)((v << 8) | (v >> 8));
#endif
}

/* Network -> host 16-bit. */
static inline uint16_t fl_net_ntohs(uint16_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_ntohs_be16(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return (uint16_t)((v << 8) | (v >> 8));
#endif
}

/* Host -> network 32-bit. */
static inline uint32_t fl_net_htonl(uint32_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_htonl_be32(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
#endif
}

/* Network -> host 32-bit. */
static inline uint32_t fl_net_ntohl(uint32_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_ntohl_be32(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
#endif
}

/* Host -> network 64-bit. Eight `0xFF` byte-masks select one byte each
 * (positions 0..7) and shift them to the mirror position. Used for any
 * future P3 wire field wider than 32 bits and for 64-bit MLQ keys
 * shipped over the wire. */
static inline uint64_t fl_net_htonll(uint64_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_htonll_be64(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return ((v & 0x00000000000000FFull) << 56) |
           ((v & 0x000000000000FF00ull) << 40) |
           ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) << 8)  |
           ((v & 0x000000FF00000000ull) >> 8)  |
           ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) |
           ((v & 0xFF00000000000000ull) >> 56);
#endif
}

/* Network -> host 64-bit. */
static inline uint64_t fl_net_ntohll(uint64_t v) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_ntohll_be64(v);
#else
    if (FL_NET_HOST_IS_BE)
        return v;
    return ((v & 0x00000000000000FFull) << 56) |
           ((v & 0x000000000000FF00ull) << 40) |
           ((v & 0x0000000000FF0000ull) << 24) |
           ((v & 0x00000000FF000000ull) << 8)  |
           ((v & 0x000000FF00000000ull) >> 8)  |
           ((v & 0x0000FF0000000000ull) >> 24) |
           ((v & 0x00FF000000000000ull) >> 40) |
           ((v & 0xFF00000000000000ull) >> 56);
#endif
}

/* Write a uint16 host-value as 2 network-byte-order bytes at out[0..1]. */
static inline void fl_net_put_u16_be(uint8_t *out, uint16_t host_value) {
    out[0] = (uint8_t)((host_value >> 8) & 0xFFu);
    out[1] = (uint8_t)(host_value & 0xFFu);
}

/* Read a uint16 host-value from 2 network-byte-order bytes at in[0..1]. */
static inline uint16_t fl_net_get_u16_be(const uint8_t *in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

/* Copy the 4 raw bytes of a network-byte-order uint32 (e.g. an IPv4 address
 * stored as `s_addr`) into out[0..3]. Endianness-independent: the wire
 * sees the IPv4 octets in the same order regardless of host endianness. */
static inline void fl_net_put_u32_nbo(uint8_t *out, uint32_t value_nbo) {
    memcpy(out, &value_nbo, 4u);
}

/* Inverse of fl_net_put_u32_nbo: copy the 4 wire bytes into a uint32 whose
 * memory layout is network byte order (suitable for `s_addr`). */
static inline uint32_t fl_net_get_u32_nbo(const uint8_t *in) {
    uint32_t value_nbo = 0u;
    memcpy(&value_nbo, in, 4u);
    return value_nbo;
}

/* Write a uint32 host-value as 4 network-byte-order bytes at out[0..3]. */
static inline void fl_net_put_u32_be(uint8_t *out, uint32_t host_value) {
    out[0] = (uint8_t)((host_value >> 24) & 0xFFu);
    out[1] = (uint8_t)((host_value >> 16) & 0xFFu);
    out[2] = (uint8_t)((host_value >> 8) & 0xFFu);
    out[3] = (uint8_t)(host_value & 0xFFu);
}

/* Read a uint32 host-value from 4 network-byte-order bytes at in[0..3]. */
static inline uint32_t fl_net_get_u32_be(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8)  |
           (uint32_t)in[3];
}

/* Write a uint64 host-value as 8 network-byte-order bytes at out[0..7]. */
static inline void fl_net_put_u64_be(uint8_t *out, uint64_t host_value) {
    out[0] = (uint8_t)((host_value >> 56) & 0xFFu);
    out[1] = (uint8_t)((host_value >> 48) & 0xFFu);
    out[2] = (uint8_t)((host_value >> 40) & 0xFFu);
    out[3] = (uint8_t)((host_value >> 32) & 0xFFu);
    out[4] = (uint8_t)((host_value >> 24) & 0xFFu);
    out[5] = (uint8_t)((host_value >> 16) & 0xFFu);
    out[6] = (uint8_t)((host_value >> 8) & 0xFFu);
    out[7] = (uint8_t)(host_value & 0xFFu);
}

/* Read a uint64 host-value from 8 network-byte-order bytes at in[0..7]. */
static inline uint64_t fl_net_get_u64_be(const uint8_t *in) {
    return ((uint64_t)in[0] << 56) |
           ((uint64_t)in[1] << 48) |
           ((uint64_t)in[2] << 40) |
           ((uint64_t)in[3] << 32) |
           ((uint64_t)in[4] << 24) |
           ((uint64_t)in[5] << 16) |
           ((uint64_t)in[6] << 8)  |
           (uint64_t)in[7];
}

#endif /* FL_NET_ENDIAN_H */
