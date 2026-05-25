#include "net_checksum.h"

#include "fl/net_asm.h"

#if !defined(FL_NET_ASM_AVAILABLE)
#define FL_NET_CHECKSUM16_ACCUM(sum, p, len) \
    do { \
        const uint8_t *_fl_cs_p = (const uint8_t *)(p); \
        size_t _fl_cs_n = (len); \
        while (_fl_cs_n > 1) { \
            (sum) += ((uint16_t)_fl_cs_p[0] << 8) | (uint16_t)_fl_cs_p[1]; \
            _fl_cs_p += 2; \
            _fl_cs_n -= 2; \
        } \
        if (_fl_cs_n == 1) \
            (sum) += (uint16_t)_fl_cs_p[0] << 8; \
    } while (0)

#define FL_NET_CHECKSUM16_FOLD(sum) \
    do { \
        while ((sum) >> 16) \
            (sum) = ((sum) & 0xffffu) + ((sum) >> 16); \
    } while (0)
#endif

uint16_t fl_net_checksum16(const void *data, size_t len) {
    if (!data || len == 0)
        return 0xffffu;

#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_checksum16(data, len);
#else
    {
        uint32_t sum = 0;

        FL_NET_CHECKSUM16_ACCUM(sum, data, len);
        FL_NET_CHECKSUM16_FOLD(sum);
        return (uint16_t)(~sum);
    }
#endif
}

uint16_t fl_net_ipv4_checksum(const void *hdr, size_t hdr_len) {
    return fl_net_checksum16(hdr, hdr_len);
}

uint16_t fl_net_pseudo_checksum_tcpudp(uint32_t src_be, uint32_t dst_be, uint8_t proto,
                                       const void *seg, size_t seg_len) {
#if defined(FL_NET_ASM_AVAILABLE)
    return asm_net_pseudo_checksum_tcpudp(src_be, dst_be, proto, seg, seg_len);
#else
    uint8_t pseudo[12];
    uint32_t sum = 0;

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

    FL_NET_CHECKSUM16_ACCUM(sum, pseudo, sizeof(pseudo));
    if (seg && seg_len > 0)
        FL_NET_CHECKSUM16_ACCUM(sum, seg, seg_len);
    FL_NET_CHECKSUM16_FOLD(sum);
    return (uint16_t)(~sum);
#endif
}
