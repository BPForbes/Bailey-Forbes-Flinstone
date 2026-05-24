#include "net_checksum.h"

uint16_t fl_net_checksum16(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    const uint16_t *w;
    uint32_t sum = 0;

    if (!data || len == 0)
        return 0xffffu;

    w = (const uint16_t *)p;
    while (len > 1) {
        sum += *w++;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)p[len - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum);
}

uint16_t fl_net_ipv4_checksum(const void *hdr, size_t hdr_len) {
    return fl_net_checksum16(hdr, hdr_len);
}

uint16_t fl_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
                                       const void *seg, size_t seg_len) {
    uint8_t pseudo[12];
    uint32_t sum = 0;
    size_t i;
    const uint16_t *w;

    pseudo[0] = (uint8_t)(src_be >> 24);
    pseudo[1] = (uint8_t)(src_be >> 16);
    pseudo[2] = (uint8_t)(src_be >> 8);
    pseudo[3] = (uint8_t)(src_be);
    pseudo[4] = (uint8_t)(dst_be >> 24);
    pseudo[5] = (uint8_t)(dst_be >> 16);
    pseudo[6] = (uint8_t)(dst_be >> 8);
    pseudo[7] = (uint8_t)(dst_be);
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (uint8_t)((seg_len >> 8) & 0xff);
    pseudo[11] = (uint8_t)(seg_len & 0xff);

    w = (const uint16_t *)pseudo;
    for (i = 0; i < sizeof(pseudo) / 2; i++)
        sum += w[i];

    if (seg && seg_len > 0) {
        w = (const uint16_t *)seg;
        i = 0;
        while (seg_len > 1) {
            sum += w[i++];
            seg_len -= 2;
        }
        if (seg_len == 1)
            sum += (uint16_t)((const uint8_t *)seg)[seg_len - 1 + i * 2] << 8;
    }

    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum);
}
