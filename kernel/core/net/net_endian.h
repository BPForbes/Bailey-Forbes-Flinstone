/*
 * net_endian.h — byte-order helpers for P3 wire framing.
 *
 * Why memcpy and not bit-shifts on `peer_ip_be`:
 *   `peer_ip_be` is a uint32_t whose memory bytes are already in network
 *   order (filled from POSIX `s_addr`). Reading it as a numeric value and
 *   then shifting (>> 8, >> 16, ...) yields LE-only behaviour: on a BE
 *   host the bytes come out reversed. memcpy of the storage preserves
 *   wire order on every host.
 *
 * Provides both byte-shuffling primitives (htons/ntohs/htonl/ntohl) and
 * memcpy-based serializers for "already in network byte order" uint16/u32.
 *
 * Tracked by issue #284.
 */
#ifndef FL_NET_ENDIAN_H
#define FL_NET_ENDIAN_H

#include <stdint.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
#include <arpa/inet.h>
#define FL_NET_ENDIAN_HAVE_POSIX 1
#endif

/* Host -> network 16-bit. */
static inline uint16_t fl_net_htons(uint16_t v) {
#if defined(FL_NET_ENDIAN_HAVE_POSIX)
    return htons(v);
#else
    return (uint16_t)((v << 8) | (v >> 8));
#endif
}

/* Network -> host 16-bit. */
static inline uint16_t fl_net_ntohs(uint16_t v) {
#if defined(FL_NET_ENDIAN_HAVE_POSIX)
    return ntohs(v);
#else
    return (uint16_t)((v << 8) | (v >> 8));
#endif
}

/* Host -> network 32-bit. */
static inline uint32_t fl_net_htonl(uint32_t v) {
#if defined(FL_NET_ENDIAN_HAVE_POSIX)
    return htonl(v);
#else
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
#endif
}

/* Network -> host 32-bit. */
static inline uint32_t fl_net_ntohl(uint32_t v) {
#if defined(FL_NET_ENDIAN_HAVE_POSIX)
    return ntohl(v);
#else
    return fl_net_htonl(v);
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

#endif /* FL_NET_ENDIAN_H */
