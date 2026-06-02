#include "net_channel_sidecar.h"

#include "net_endian.h"
#include "net_pkt_channel_meta.h"

#include <string.h>

static fl_result_t put_cstring16(uint8_t *buf, uint16_t cap, uint16_t *off, const char *s)
{
    uint16_t n;
    if (!buf || !off)
        return FL_RESULT_INVAL;
    n = s ? (uint16_t)strnlen(s, 65535u) : 0u;
    if (*off + 2u + n > cap)
        return FL_RESULT_NOMEM;
    fl_net_put_u16_be(buf + *off, n);
    *off = (uint16_t)(*off + 2u);
    if (n > 0u) {
        memcpy(buf + *off, s, n);
        *off = (uint16_t)(*off + n);
    }
    return FL_RESULT_OK;
}

static fl_result_t get_cstring16(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                 char *dst, size_t dst_cap)
{
    uint16_t n;
    if (!buf || !off || !dst || dst_cap == 0u)
        return FL_RESULT_INVAL;
    if (*off + 2u > cap)
        return FL_RESULT_INVAL;
    n = fl_net_get_u16_be(buf + *off);
    *off = (uint16_t)(*off + 2u);
    if (*off + n > cap)
        return FL_RESULT_INVAL;
    if (n >= dst_cap)
        return FL_RESULT_INVAL;
    if (n > 0u)
        memcpy(dst, buf + *off, n);
    dst[n] = '\0';
    *off = (uint16_t)(*off + n);
    return FL_RESULT_OK;
}

fl_result_t fl_channel_sidecar_encode(const fl_channel_sidecar_t *sidecar,
                                      uint8_t *out,
                                      uint16_t out_cap,
                                      uint16_t *out_len)
{
    uint16_t off = 0;
    fl_result_t rc;

    if (!sidecar || !out || !out_len || !sidecar->transfer_id[0])
        return FL_RESULT_INVAL;
    if (!fl_pkt_wire_valid(sidecar->pkt_meta, FL_PKT_CHANNEL_META_LEN))
        return FL_RESULT_INVAL;
    if (off + FL_PKT_CHANNEL_META_LEN + 22u > out_cap)
        return FL_RESULT_NOMEM;
    memcpy(out, sidecar->pkt_meta, FL_PKT_CHANNEL_META_LEN);
    off = FL_PKT_CHANNEL_META_LEN;
    out[off++] = sidecar->wire_rev ? sidecar->wire_rev : FL_CHANNEL_SIDECAR_WIRE_REV;
    out[off++] = sidecar->payload_kind;
    fl_net_put_u16_be(out + off, sidecar->sender_member_id);
    off = (uint16_t)(off + 2u);
    fl_net_put_u16_be(out + off, sidecar->receiver_member_id);
    off = (uint16_t)(off + 2u);
    fl_net_put_u64_be(out + off, sidecar->expires_at);
    off = (uint16_t)(off + 8u);
    fl_net_put_u64_be(out + off, sidecar->center_freq_hz);
    off = (uint16_t)(off + 8u);
    rc = put_cstring16(out, out_cap, &off, sidecar->transfer_id);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = put_cstring16(out, out_cap, &off, sidecar->payload_name);
    if (rc != FL_RESULT_OK)
        return rc;
    *out_len = off;
    return FL_RESULT_OK;
}

static fl_result_t decode_legacy_file_meta(const uint8_t *payload,
                                           uint16_t payload_len,
                                           fl_channel_sidecar_t *out)
{
    uint16_t off = 0;
    fl_result_t rc;
    memset(out, 0, sizeof(*out));
    rc = get_cstring16(payload, payload_len, &off,
                       out->transfer_id, sizeof(out->transfer_id));
    if (rc != FL_RESULT_OK)
        return rc;
    if (off + 8u > payload_len)
        return FL_RESULT_INVAL;
    out->expires_at = fl_net_get_u64_be(payload + off);
    off = (uint16_t)(off + 8u);
    rc = get_cstring16(payload, payload_len, &off,
                       out->payload_name, sizeof(out->payload_name));
    if (rc != FL_RESULT_OK)
        return rc;
    out->payload_kind = FL_CHANNEL_PAYLOAD_FILE;
    out->wire_rev = 0u;
    return FL_RESULT_OK;
}

fl_result_t fl_channel_sidecar_decode(const uint8_t *payload,
                                      uint16_t payload_len,
                                      fl_channel_sidecar_t *out)
{
    uint16_t off = 0;
    fl_result_t rc;

    if (!payload || !out)
        return FL_RESULT_INVAL;
    if (payload_len < FL_PKT_CHANNEL_META_LEN)
        return decode_legacy_file_meta(payload, payload_len, out);
    if (!fl_pkt_wire_has_meta(payload, payload_len))
        return decode_legacy_file_meta(payload, payload_len, out);
    if ((payload[0] & FL_PKT_TYPE) == 0u && (payload[0] & FL_PKT_VALID) == 0u)
        return decode_legacy_file_meta(payload, payload_len, out);

    memset(out, 0, sizeof(*out));
    memcpy(out->pkt_meta, payload, FL_PKT_CHANNEL_META_LEN);
    off = FL_PKT_CHANNEL_META_LEN;
    if (off + 22u > payload_len)
        return FL_RESULT_INVAL;
    out->wire_rev = payload[off++];
    out->payload_kind = payload[off++];
    out->sender_member_id = fl_net_get_u16_be(payload + off);
    off = (uint16_t)(off + 2u);
    out->receiver_member_id = fl_net_get_u16_be(payload + off);
    off = (uint16_t)(off + 2u);
    out->expires_at = fl_net_get_u64_be(payload + off);
    off = (uint16_t)(off + 8u);
    out->center_freq_hz = fl_net_get_u64_be(payload + off);
    off = (uint16_t)(off + 8u);
    rc = get_cstring16(payload, payload_len, &off,
                       out->transfer_id, sizeof(out->transfer_id));
    if (rc != FL_RESULT_OK)
        return rc;
    rc = get_cstring16(payload, payload_len, &off,
                       out->payload_name, sizeof(out->payload_name));
    return rc;
}

int fl_channel_sidecar_may_forward_to(uint16_t receiver_member_id,
                                      uint16_t target_member_id)
{
    if (receiver_member_id == 0u)
        return 1;
    return receiver_member_id == target_member_id;
}
