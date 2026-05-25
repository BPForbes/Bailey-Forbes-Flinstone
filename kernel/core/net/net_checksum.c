#include "net_checksum.h"

static uint32_t checksum16_accum(const uint8_t *p, size_t len) {
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | (uint16_t)p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1)
        sum += (uint16_t)p[0] << 8;
    return sum;
}

uint16_t fl_net_checksum16(const void *data, size_t len) {
    uint32_t sum;

    if (!data || len == 0)
        return 0xffffu;

    sum = checksum16_accum((const uint8_t *)data, len);
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
    uint32_t sum;

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

    sum = checksum16_accum(pseudo, sizeof(pseudo));
    if (seg && seg_len > 0)
        sum += checksum16_accum((const uint8_t *)seg, seg_len);

    while (sum >> 16)
        sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum);
}
