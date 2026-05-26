#include "net_udp.h"

#include "contract_p3_ipv4.h"
#include "net_checksum.h"

#include <string.h>

size_t fl_net_udp_build_datagram(uint8_t *buf, size_t cap, uint32_t src_be, uint32_t dst_be,
                                 uint16_t sport_host, uint16_t dport_host,
                                 const uint8_t *payload, size_t payload_len) {
    uint16_t udp_len;
    uint16_t csum;
    size_t total;

    if (!buf || payload_len > FL_NET_CONTRACT_MAX_UDP_DATAGRAM)
        return 0;
    total = (size_t)FL_NET_UDP_HDR_LEN + payload_len;
    if (cap < total)
        return 0;

    udp_len = (uint16_t)total;

    buf[0] = (uint8_t)(sport_host >> 8);
    buf[1] = (uint8_t)(sport_host & 0xff);
    buf[2] = (uint8_t)(dport_host >> 8);
    buf[3] = (uint8_t)(dport_host & 0xff);
    buf[4] = (uint8_t)(udp_len >> 8);
    buf[5] = (uint8_t)(udp_len & 0xff);
    buf[6] = 0;
    buf[7] = 0;

    if (payload_len > 0 && payload)
        memcpy(buf + FL_NET_UDP_HDR_LEN, payload, payload_len);

    csum = fl_net_pseudo_checksum_tcpudp(src_be, dst_be, FL_NET_IP_PROTO_UDP, buf, total);
    buf[6] = (uint8_t)(csum >> 8);
    buf[7] = (uint8_t)(csum & 0xff);

    return total;
}
