#include "net_udp.h"

#include "contract_p3_ipv4.h"
#include "net_checksum.h"

#include "fl/mem_asm.h"
#include "fl/net_asm.h"

#include <string.h>

/** Store **host** port/length as big-endian wire octets (uses **asm_net_htons_be16**). */
static void net_udp_store_be16(uint8_t *dst, uint16_t host) {
    uint16_t n;

#if defined(FL_NET_ASM_AVAILABLE)
    n = asm_net_htons_be16(host);
#else
    n = (uint16_t)((host >> 8) | (host << 8));
#endif
    dst[0] = ((const uint8_t *)&n)[0];
    dst[1] = ((const uint8_t *)&n)[1];
}

size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len) {
    uint16_t csum;
    size_t total;

    if (!buf || payload_len > FL_NET_CONTRACT_MAX_UDP_DATAGRAM)
        return 0;
    total = (size_t)FL_NET_UDP_HDR_LEN + payload_len;
    if (cap < total)
        return 0;

    net_udp_store_be16(buf + 0, sport_host);
    net_udp_store_be16(buf + 2, dport_host);
    net_udp_store_be16(buf + 4, (uint16_t)total);
    buf[6] = 0;
    buf[7] = 0;

    if (payload_len > 0 && payload) {
#if defined(FL_NET_ASM_AVAILABLE)
        asm_mem_copy(buf + FL_NET_UDP_HDR_LEN, payload, payload_len);
#else
        memcpy(buf + FL_NET_UDP_HDR_LEN, payload, payload_len);
#endif
    }

#if defined(FL_NET_ASM_AVAILABLE)
    csum = asm_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_UDP, buf, total);
#else
    csum = fl_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_UDP, buf, total);
#endif
    buf[6] = (uint8_t)(csum >> 8);
    buf[7] = (uint8_t)(csum & 0xff);

    return total;
}
