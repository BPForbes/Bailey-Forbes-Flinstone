#include "net_file_delivery.h"

#include "contract_p3_packet.h"
#include "net_endian.h"
#include "server_shared_fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FL_SERVER_FILE_LOOKUP_MAX FL_NET_SERVER_MAX_MEMBERS
#define FL_SERVER_FILE_SHARE_SLOTS 32u
#define FL_SERVER_FILE_MAX_BYTES (16u * 1024u * 1024u)

typedef struct {
    uint8_t active;
    uint8_t transfer_complete;
    uint8_t meta_received;
    uint8_t *file_data;
    size_t file_len;
    size_t file_cap;
    fl_server_file_offer_t offer;
} fl_server_file_share_slot_t;

static fl_server_file_member_ref_t s_lookup[FL_SERVER_FILE_LOOKUP_MAX];
static size_t s_lookup_count;
static fl_server_file_share_slot_t s_shares[FL_SERVER_FILE_SHARE_SLOTS];
static uint32_t s_share_serial;

static fl_result_t put_u16(fl_bytes_writer_t *w, uint16_t v)
{
    if (!w || w->len + 2u > w->cap)
        return FL_RESULT_NOMEM;
    w->buf[w->len++] = (uint8_t)((v >> 8) & 0xFFu);
    w->buf[w->len++] = (uint8_t)(v & 0xFFu);
    return FL_RESULT_OK;
}

static fl_result_t put_u32(fl_bytes_writer_t *w, uint32_t v)
{
    if (!w || w->len + 4u > w->cap)
        return FL_RESULT_NOMEM;
    w->buf[w->len++] = (uint8_t)((v >> 24) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 16) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 8) & 0xFFu);
    w->buf[w->len++] = (uint8_t)(v & 0xFFu);
    return FL_RESULT_OK;
}

static fl_result_t put_u64(fl_bytes_writer_t *w, uint64_t v)
{
    if (!w || w->len + 8u > w->cap)
        return FL_RESULT_NOMEM;
    w->buf[w->len++] = (uint8_t)((v >> 56) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 48) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 40) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 32) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 24) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 16) & 0xFFu);
    w->buf[w->len++] = (uint8_t)((v >> 8) & 0xFFu);
    w->buf[w->len++] = (uint8_t)(v & 0xFFu);
    return FL_RESULT_OK;
}

static fl_result_t get_u16(const uint8_t *buf, uint16_t cap, uint16_t *off, uint16_t *out)
{
    if (!buf || !off || !out || *off + 2u > cap)
        return FL_RESULT_INVAL;
    *out = (uint16_t)(((uint16_t)buf[*off] << 8) | buf[*off + 1u]);
    *off = (uint16_t)(*off + 2u);
    return FL_RESULT_OK;
}

static fl_result_t get_u32(const uint8_t *buf, uint16_t cap, uint16_t *off, uint32_t *out)
{
    if (!buf || !off || !out || *off + 4u > cap)
        return FL_RESULT_INVAL;
    *out = ((uint32_t)buf[*off] << 24) | ((uint32_t)buf[*off + 1u] << 16) |
           ((uint32_t)buf[*off + 2u] << 8) | (uint32_t)buf[*off + 3u];
    *off = (uint16_t)(*off + 4u);
    return FL_RESULT_OK;
}

static fl_result_t get_u64(const uint8_t *buf, uint16_t cap, uint16_t *off, uint64_t *out)
{
    uint32_t hi;
    uint32_t lo;
    fl_result_t rc;

    if (!buf || !off || !out)
        return FL_RESULT_INVAL;
    rc = get_u32(buf, cap, off, &hi);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = get_u32(buf, cap, off, &lo);
    if (rc != FL_RESULT_OK)
        return rc;
    *out = ((uint64_t)hi << 32) | (uint64_t)lo;
    return FL_RESULT_OK;
}

static void copy_str_field(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0u)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

static void gen_share_id(char *out, size_t cap)
{
    time_t now = time(NULL);
    s_share_serial++;
    snprintf(out, cap, "share-%lu-%u", (unsigned long)now, s_share_serial);
}

static fl_server_file_share_slot_t *share_find(const char *share_id)
{
    size_t i;
    if (!share_id || !share_id[0])
        return NULL;
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        if (s_shares[i].active &&
            strcmp(s_shares[i].offer.share_id, share_id) == 0)
            return &s_shares[i];
    }
    return NULL;
}

static void share_slot_free_data(fl_server_file_share_slot_t *slot)
{
    if (!slot)
        return;
    free(slot->file_data);
    slot->file_data = NULL;
    slot->file_len = 0u;
    slot->file_cap = 0u;
    slot->transfer_complete = 0u;
}

static void share_slot_release(fl_server_file_share_slot_t *slot)
{
    if (!slot)
        return;
    share_slot_free_data(slot);
    slot->active = 0u;
}

static fl_result_t share_slot_ensure_cap(fl_server_file_share_slot_t *slot, size_t need)
{
    uint8_t *nbuf;
    if (!slot)
        return FL_RESULT_INVAL;
    if (need > FL_SERVER_FILE_MAX_BYTES)
        return FL_RESULT_NOMEM;
    if (need <= slot->file_cap)
        return FL_RESULT_OK;
    nbuf = (uint8_t *)realloc(slot->file_data, need);
    if (!nbuf)
        return FL_RESULT_NOMEM;
    slot->file_data = nbuf;
    slot->file_cap = need;
    return FL_RESULT_OK;
}

static void share_purge_expired(uint64_t now)
{
    size_t i;
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        fl_server_file_share_slot_t *slot = &s_shares[i];
        if (!slot->active)
            continue;
        if (fl_file_share_validate_access(slot->offer.file_perms,
                                          slot->offer.expires_at,
                                          now) == FL_RESULT_OK)
            continue;
        slot->offer.file_perms |= FL_FILE_FLAG_REVOKED;
        share_slot_release(slot);
    }
}

static fl_result_t share_access_ok(const fl_server_file_offer_t *offer, uint64_t now)
{
    if (!offer)
        return FL_RESULT_INVAL;
    return fl_file_share_validate_access(offer->file_perms, offer->expires_at, now);
}

static void append_expiry_notice(char *out, uint16_t out_cap, uint64_t expires_at)
{
    time_t t;
    struct tm tmv;
    char suffix[96];
    size_t used;
    if (!out || out_cap == 0u || expires_at == 0u)
        return;
    t = (time_t)expires_at;
    if (!gmtime_r(&t, &tmv))
        return;
    snprintf(suffix, sizeof(suffix),
             " (expires %02d-%02d-%04d %02d:%02d:%02d UTC)",
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_year + 1900,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    used = strlen(out);
    if (used >= out_cap)
        return;
    snprintf(out + used, (size_t)out_cap - used, "%s", suffix);
}

static fl_server_file_share_slot_t *share_alloc(const fl_server_file_offer_t *offer)
{
    size_t i;
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        if (!s_shares[i].active) {
            share_slot_free_data(&s_shares[i]);
            s_shares[i].active = 1u;
            s_shares[i].meta_received = 0u;
            s_shares[i].offer = *offer;
            if (offer->file_size == 0u)
                s_shares[i].transfer_complete = 1u;
            return &s_shares[i];
        }
    }
    return NULL;
}

void fl_server_file_lookup_begin(void)
{
    s_lookup_count = 0u;
}

fl_result_t fl_server_file_lookup_push(const fl_server_file_member_ref_t *entry)
{
    if (!entry || s_lookup_count >= FL_SERVER_FILE_LOOKUP_MAX)
        return FL_RESULT_NOMEM;
    s_lookup[s_lookup_count++] = *entry;
    return FL_RESULT_OK;
}

fl_result_t fl_wire_put_bytes16(fl_bytes_writer_t *w,
                                const uint8_t *data,
                                uint16_t len)
{
    fl_result_t rc;
    if (!w)
        return FL_RESULT_INVAL;
    rc = put_u16(w, len);
    if (rc != FL_RESULT_OK)
        return rc;
    if (len > 0u) {
        if (w->len + len > w->cap)
            return FL_RESULT_NOMEM;
        if (data)
            memcpy(w->buf + w->len, data, len);
        else
            memset(w->buf + w->len, 0, len);
        w->len = (uint16_t)(w->len + len);
    }
    return FL_RESULT_OK;
}

fl_result_t fl_wire_get_bytes16(const uint8_t *buf,
                                uint16_t cap,
                                uint16_t *offset,
                                fl_bytes_view_t *out)
{
    uint16_t len;
    fl_result_t rc;
    if (!buf || !offset || !out)
        return FL_RESULT_INVAL;
    rc = get_u16(buf, cap, offset, &len);
    if (rc != FL_RESULT_OK)
        return rc;
    if (*offset + len > cap)
        return FL_RESULT_INVAL;
    out->data = len > 0u ? buf + *offset : NULL;
    out->len = len;
    *offset = (uint16_t)(*offset + len);
    return FL_RESULT_OK;
}

static fl_result_t put_cstring16(fl_bytes_writer_t *w, const char *s)
{
    uint16_t n = s ? (uint16_t)strnlen(s, 65535u) : 0u;
    return fl_wire_put_bytes16(w, (const uint8_t *)s, n);
}

static fl_result_t get_cstring16(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                 char *dst, size_t dst_cap)
{
    fl_bytes_view_t view;
    fl_result_t rc = fl_wire_get_bytes16(buf, cap, off, &view);
    if (rc != FL_RESULT_OK)
        return rc;
    if (view.len >= dst_cap)
        return FL_RESULT_INVAL;
    if (view.len > 0u)
        memcpy(dst, view.data, view.len);
    dst[view.len] = '\0';
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_offer_encode(const fl_server_file_offer_t *offer,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;

    if (!offer || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;

    rc = put_u16(&w, offer->sender_member_id);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u16(&w, offer->receiver_member_id);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u16(&w, offer->file_perms);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u16(&w, offer->reserved0);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u64(&w, offer->created_at);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u64(&w, offer->expires_at);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u64(&w, offer->file_size);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u32(&w, offer->chunk_size);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_u32(&w, offer->total_chunks);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->share_id);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->sender_path);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->suggested_dest_path);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->file_name);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->sender_display);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->receiver_display);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->sender_principal);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->receiver_principal);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->sender_nick_snapshot);
    if (rc != FL_RESULT_OK) goto fail;
    rc = put_cstring16(&w, offer->receiver_nick_snapshot);
    if (rc != FL_RESULT_OK) goto fail;
    rc = fl_wire_put_bytes16(&w, offer->checksum, FL_SERVER_FILE_HASH_MAX);
    if (rc != FL_RESULT_OK) goto fail;

    *out_len = w.len;
    return FL_RESULT_OK;
fail:
    return rc;
}

fl_result_t fl_server_file_offer_decode(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_offer_t *out)
{
    uint16_t off = 0;
    fl_result_t rc;

    if (!payload || !out)
        return FL_RESULT_INVAL;
    if (payload_len < FL_FILE_OFFER_WIRE_MIN_LEN)
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));

    if (fl_pkt_file_offer_has_meta_layout(payload, payload_len))
        off = (uint16_t)FL_PKT_CHANNEL_META_LEN;

    rc = get_u16(payload, payload_len, &off, &out->sender_member_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u16(payload, payload_len, &off, &out->receiver_member_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u16(payload, payload_len, &off, &out->file_perms);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u16(payload, payload_len, &off, &out->reserved0);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u64(payload, payload_len, &off, &out->created_at);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u64(payload, payload_len, &off, &out->expires_at);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u64(payload, payload_len, &off, &out->file_size);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u32(payload, payload_len, &off, &out->chunk_size);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u32(payload, payload_len, &off, &out->total_chunks);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->share_id, sizeof(out->share_id));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->sender_path, sizeof(out->sender_path));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->suggested_dest_path,
                       sizeof(out->suggested_dest_path));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->file_name, sizeof(out->file_name));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->sender_display, sizeof(out->sender_display));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->receiver_display,
                       sizeof(out->receiver_display));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->sender_principal,
                       sizeof(out->sender_principal));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->receiver_principal,
                       sizeof(out->receiver_principal));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->sender_nick_snapshot,
                       sizeof(out->sender_nick_snapshot));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_cstring16(payload, payload_len, &off, out->receiver_nick_snapshot,
                       sizeof(out->receiver_nick_snapshot));
    if (rc != FL_RESULT_OK) return rc;
    {
        fl_bytes_view_t hash;
        rc = fl_wire_get_bytes16(payload, payload_len, &off, &hash);
        if (rc != FL_RESULT_OK) return rc;
        if (hash.len != FL_SERVER_FILE_HASH_MAX)
            return FL_RESULT_INVAL;
        memcpy(out->checksum, hash.data, FL_SERVER_FILE_HASH_MAX);
    }
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_offer_create(fl_server_file_offer_t *out,
                                        uint16_t sender_id,
                                        uint16_t receiver_id,
                                        const char *sender_path,
                                        const char *suggested_dest,
                                        fl_file_perms_t file_perms,
                                        uint64_t expires_at)
{
    const char *base;
    if (!out || !sender_path || !sender_path[0])
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));
    gen_share_id(out->share_id, sizeof(out->share_id));
    out->sender_member_id = sender_id;
    out->receiver_member_id = receiver_id;
    out->file_perms = fl_file_perms_normalize(file_perms);
    out->created_at = (uint64_t)time(NULL);
    out->expires_at = expires_at;
    if (out->expires_at != 0u)
        out->file_perms |= FL_FILE_FLAG_EXPIRES;
    copy_str_field(out->sender_path, sizeof(out->sender_path), sender_path);
    copy_str_field(out->suggested_dest_path, sizeof(out->suggested_dest_path),
                   suggested_dest ? suggested_dest : sender_path);
    base = strrchr(sender_path, '/');
    base = base ? base + 1 : sender_path;
    copy_str_field(out->file_name, sizeof(out->file_name), base);
    if (receiver_id == 0u)
        out->file_perms |= FL_FILE_FLAG_PUBLIC;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_offer_validate(const fl_server_file_offer_t *offer,
                                          uint16_t current_member_id,
                                          uint64_t now)
{
    (void)current_member_id;
    if (!offer || !offer->share_id[0])
        return FL_RESULT_INVAL;
    return fl_file_share_validate_access(offer->file_perms, offer->expires_at, now);
}

fl_result_t fl_server_file_chunk_encode(const fl_server_file_chunk_header_t *chunk,
                                        const uint8_t *data,
                                        uint32_t data_len,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!chunk || !out || !out_len)
        return FL_RESULT_INVAL;
    if (data_len > FL_SERVER_FILE_CHUNK_MAX)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, chunk->share_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u32(&w, chunk->chunk_index);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u32(&w, chunk->chunk_len);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u64(&w, chunk->file_offset);
    if (rc != FL_RESULT_OK) return rc;
    rc = fl_wire_put_bytes16(&w, data, (uint16_t)data_len);
    if (rc != FL_RESULT_OK) return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_chunk_decode(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_chunk_header_t *chunk,
                                        fl_bytes_view_t *data)
{
    uint16_t off = 0;
    fl_result_t rc;
    if (!payload || !chunk || !data)
        return FL_RESULT_INVAL;
    memset(chunk, 0, sizeof(*chunk));
    rc = get_cstring16(payload, payload_len, &off, chunk->share_id, sizeof(chunk->share_id));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u32(payload, payload_len, &off, &chunk->chunk_index);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u32(payload, payload_len, &off, &chunk->chunk_len);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u64(payload, payload_len, &off, &chunk->file_offset);
    if (rc != FL_RESULT_OK) return rc;
    return fl_wire_get_bytes16(payload, payload_len, &off, data);
}

fl_result_t fl_file_packet_encode_offer(const fl_server_file_offer_t *offer,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len)
{
    return fl_server_file_offer_encode(offer, out, out_cap, out_len);
}

fl_result_t fl_file_packet_decode_offer(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_offer_t *out)
{
    return fl_server_file_offer_decode(payload, payload_len, out);
}

fl_result_t fl_file_packet_encode_chunk(const fl_server_file_chunk_header_t *chunk,
                                        const uint8_t *data,
                                        uint32_t data_len,
                                        uint8_t *out,
                                        uint16_t out_cap,
                                        uint16_t *out_len)
{
    return fl_server_file_chunk_encode(chunk, data, data_len, out, out_cap, out_len);
}

fl_result_t fl_file_packet_decode_chunk(const uint8_t *payload,
                                        uint16_t payload_len,
                                        fl_server_file_chunk_header_t *chunk,
                                        fl_bytes_view_t *data)
{
    return fl_server_file_chunk_decode(payload, payload_len, chunk, data);
}

fl_result_t fl_file_packet_encode_done(const fl_server_file_done_t *done,
                                       uint8_t *out,
                                       uint16_t out_cap,
                                       uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!done || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, done->share_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u64(&w, done->total_bytes);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u32(&w, done->total_chunks);
    if (rc != FL_RESULT_OK) return rc;
    rc = fl_wire_put_bytes16(&w, done->checksum, FL_SERVER_FILE_HASH_MAX);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u16(&w, done->status);
    if (rc != FL_RESULT_OK) return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_decode_done(const uint8_t *payload,
                                       uint16_t payload_len,
                                       fl_server_file_done_t *done)
{
    uint16_t off = 0;
    fl_result_t rc;
    fl_bytes_view_t hash;
    if (!payload || !done)
        return FL_RESULT_INVAL;
    memset(done, 0, sizeof(*done));
    rc = get_cstring16(payload, payload_len, &off, done->share_id, sizeof(done->share_id));
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u64(payload, payload_len, &off, &done->total_bytes);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u32(payload, payload_len, &off, &done->total_chunks);
    if (rc != FL_RESULT_OK) return rc;
    rc = fl_wire_get_bytes16(payload, payload_len, &off, &hash);
    if (rc != FL_RESULT_OK) return rc;
    if (hash.len != FL_SERVER_FILE_HASH_MAX)
        return FL_RESULT_INVAL;
    memcpy(done->checksum, hash.data, FL_SERVER_FILE_HASH_MAX);
    rc = get_u16(payload, payload_len, &off, (uint16_t *)&done->status);
    return rc;
}

fl_result_t fl_file_packet_encode_accept(const char *share_id,
                                         uint16_t receiver_member_id,
                                         fl_server_file_disposition_t disposition,
                                         uint8_t *out,
                                         uint16_t out_cap,
                                         uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!share_id || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, share_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u16(&w, receiver_member_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u16(&w, (uint16_t)disposition);
    if (rc != FL_RESULT_OK) return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_decode_accept(const uint8_t *payload,
                                         uint16_t payload_len,
                                         char *share_id,
                                         uint16_t share_id_cap,
                                         uint16_t *receiver_member_id,
                                         fl_server_file_disposition_t *disposition)
{
    uint16_t off = 0;
    uint16_t disp = 0;
    fl_result_t rc;
    if (!payload || !share_id || !receiver_member_id || !disposition)
        return FL_RESULT_INVAL;
    rc = get_cstring16(payload, payload_len, &off, share_id, share_id_cap);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u16(payload, payload_len, &off, receiver_member_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = get_u16(payload, payload_len, &off, &disp);
    if (rc != FL_RESULT_OK) return rc;
    *disposition = (fl_server_file_disposition_t)disp;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_encode_decline(const char *share_id,
                                          uint16_t receiver_member_id,
                                          uint8_t *out,
                                          uint16_t out_cap,
                                          uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!share_id || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, share_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u16(&w, receiver_member_id);
    if (rc != FL_RESULT_OK) return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_decode_decline(const uint8_t *payload,
                                          uint16_t payload_len,
                                          char *share_id,
                                          uint16_t share_id_cap,
                                          uint16_t *receiver_member_id)
{
    uint16_t off = 0;
    fl_result_t rc;
    if (!payload || !share_id || !receiver_member_id)
        return FL_RESULT_INVAL;
    rc = get_cstring16(payload, payload_len, &off, share_id, share_id_cap);
    if (rc != FL_RESULT_OK) return rc;
    return get_u16(payload, payload_len, &off, receiver_member_id);
}

fl_result_t fl_file_packet_encode_revoke(const char *share_id,
                                         uint16_t owner_member_id,
                                         uint8_t *out,
                                         uint16_t out_cap,
                                         uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!share_id || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, share_id);
    if (rc != FL_RESULT_OK) return rc;
    rc = put_u16(&w, owner_member_id);
    if (rc != FL_RESULT_OK) return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_decode_revoke(const uint8_t *payload,
                                         uint16_t payload_len,
                                         char *share_id,
                                         uint16_t share_id_cap,
                                         uint16_t *owner_member_id)
{
    uint16_t off = 0;
    fl_result_t rc;
    if (!payload || !share_id || !owner_member_id)
        return FL_RESULT_INVAL;
    rc = get_cstring16(payload, payload_len, &off, share_id, share_id_cap);
    if (rc != FL_RESULT_OK) return rc;
    return get_u16(payload, payload_len, &off, owner_member_id);
}

fl_result_t fl_net_session_validate_file_frame(uint8_t opcode,
                                               uint8_t flags,
                                               uint16_t payload_len)
{
    (void)flags;
    if (!fl_net_session_is_file_opcode(opcode))
        return FL_RESULT_INVAL;
    if (payload_len > FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    if (opcode == FL_NET_SESSION_OP_FILE_CHUNK &&
        payload_len > FL_NET_SESSION_MAX_MSG)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

static int name_matches(const fl_server_file_member_ref_t *m,
                        const char *target_name,
                        uint16_t disambig)
{
    if (!m || !target_name || !target_name[0])
        return 0;
    if (m->nick[0] && strcmp(m->nick, target_name) == 0)
        return 1;
    if (disambig != 0u) {
        return m->disambig_index == (uint8_t)disambig &&
               strcmp(m->principal, target_name) == 0;
    }
    return strcmp(m->principal, target_name) == 0;
}

fl_result_t fl_server_file_resolve_receiver(const char *target_name,
                                            uint16_t explicit_member_id,
                                            uint16_t sender_member_id,
                                            uint16_t *out_receiver_member_id)
{
    size_t i;
    size_t matches = 0u;
    uint16_t found = 0u;

    if (!out_receiver_member_id)
        return FL_RESULT_INVAL;
    *out_receiver_member_id = 0u;

    if (explicit_member_id != 0u) {
        if (explicit_member_id == sender_member_id)
            return FL_RESULT_INVAL;
        *out_receiver_member_id = explicit_member_id;
        return FL_RESULT_OK;
    }
    if (!target_name || !target_name[0])
        return FL_RESULT_INVAL;

    for (i = 0; i < s_lookup_count; i++) {
        if (s_lookup[i].member_id == sender_member_id)
            continue;
        if (!name_matches(&s_lookup[i], target_name, 0u))
            continue;
        matches++;
        found = s_lookup[i].member_id;
    }
    if (matches == 0u)
        return FL_RESULT_NOENT;
    if (matches > 1u)
        return FL_RESULT_BUSY;
    *out_receiver_member_id = found;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_snapshot_display_names(uint16_t sender_member_id,
                                                  uint16_t receiver_member_id,
                                                  char *sender_display,
                                                  uint16_t sender_display_cap,
                                                  char *receiver_display,
                                                  uint16_t receiver_display_cap)
{
    size_t i;
    int sender_done = 0;
    int receiver_done = 0;

    if (!sender_display || !receiver_display ||
        sender_display_cap == 0u || receiver_display_cap == 0u)
        return FL_RESULT_INVAL;
    sender_display[0] = '\0';
    receiver_display[0] = '\0';

    for (i = 0; i < s_lookup_count; i++) {
        char tmp[FL_NET_SERVER_DISPLAY_NAME_MAX];
        const fl_server_file_member_ref_t *m = &s_lookup[i];
        if (m->nick[0])
            snprintf(tmp, sizeof(tmp), "%s", m->nick);
        else if (m->disambig_index != 0u)
            snprintf(tmp, sizeof(tmp), "%s {%u}", m->principal,
                     (unsigned)m->disambig_index);
        else
            snprintf(tmp, sizeof(tmp), "%s", m->principal);

        if (!sender_done && m->member_id == sender_member_id) {
            copy_str_field(sender_display, sender_display_cap, tmp);
            sender_done = 1;
        }
        if (!receiver_done && receiver_member_id != 0u &&
            m->member_id == receiver_member_id) {
            copy_str_field(receiver_display, receiver_display_cap, tmp);
            receiver_done = 1;
        }
    }
    if (receiver_member_id == 0u)
        copy_str_field(receiver_display, receiver_display_cap, "All");
    if (!sender_done)
        return FL_RESULT_NOENT;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_render_offer_line(const fl_server_file_offer_t *offer,
                                             uint16_t viewer_member_id,
                                             char *out,
                                             uint16_t out_cap)
{
    if (!offer || !out || out_cap == 0u)
        return FL_RESULT_INVAL;
    /* Sender already got local command status; no offer echo line. */
    if (offer->sender_member_id == viewer_member_id) {
        out[0] = '\0';
        return FL_RESULT_OK;
    }
    if (offer->receiver_member_id == 0u) {
        snprintf(out, out_cap, "[Server File, From %s]: %s",
                 offer->sender_display, offer->file_name);
    } else if (offer->receiver_member_id == viewer_member_id) {
        snprintf(out, out_cap, "[Server File, %s -> You]: %s",
                 offer->sender_display, offer->file_name);
    } else {
        snprintf(out, out_cap, "[Server File, %s -> %s]: %s",
                 offer->sender_display, offer->receiver_display, offer->file_name);
    }
    if (offer->expires_at != 0u &&
        (offer->receiver_member_id == 0u ||
         offer->receiver_member_id == viewer_member_id))
        append_expiry_notice(out, out_cap, offer->expires_at);
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_inbox_list(uint16_t member_id)
{
    size_t i;
    int any = 0;
    uint64_t now = (uint64_t)time(NULL);
    share_purge_expired(now);
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        const fl_server_file_offer_t *o = &s_shares[i].offer;
        char expbuf[64];
        if (!s_shares[i].active)
            continue;
        if (share_access_ok(o, now) != FL_RESULT_OK)
            continue;
        if (o->receiver_member_id != member_id)
            continue;
        expbuf[0] = '\0';
        if (o->expires_at != 0u)
            append_expiry_notice(expbuf, (uint16_t)sizeof(expbuf), o->expires_at);
        printf("%s  %s  from %s%s\n", o->share_id, o->file_name, o->sender_display,
               expbuf);
        any = 1;
    }
    if (!any)
        printf("(inbox empty)\n");
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_public_list(void)
{
    size_t i;
    int any = 0;
    uint64_t now = (uint64_t)time(NULL);
    share_purge_expired(now);
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        const fl_server_file_offer_t *o = &s_shares[i].offer;
        if (!s_shares[i].active || o->receiver_member_id != 0u)
            continue;
        if (share_access_ok(o, now) != FL_RESULT_OK)
            continue;
        printf("%s  %s  from %s\n", o->share_id, o->file_name, o->sender_display);
        any = 1;
    }
    if (!any)
        printf("(public empty)\n");
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_sent_list(uint16_t member_id)
{
    size_t i;
    int any = 0;
    for (i = 0; i < FL_SERVER_FILE_SHARE_SLOTS; i++) {
        const fl_server_file_offer_t *o = &s_shares[i].offer;
        if (!s_shares[i].active || o->sender_member_id != member_id)
            continue;
        printf("%s  %s  to %s\n", o->share_id, o->file_name,
               o->receiver_display);
        any = 1;
    }
    if (!any)
        printf("(sent empty)\n");
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_accept(const char *share_id,
                                   uint16_t receiver_id,
                                   fl_server_file_disposition_t disposition)
{
    fl_server_file_share_slot_t *slot = share_find(share_id);
    uint64_t now = (uint64_t)time(NULL);
    fl_result_t rc;
    if (!slot)
        return FL_RESULT_NOENT;
    share_purge_expired(now);
    slot = share_find(share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    rc = share_access_ok(&slot->offer, now);
    if (rc != FL_RESULT_OK)
        return rc;
    if (slot->offer.receiver_member_id != 0u &&
        slot->offer.receiver_member_id != receiver_id)
        return FL_RESULT_ACCES;
    if (!slot->transfer_complete && slot->offer.file_size > 0u)
        return FL_RESULT_BUSY;
    if (disposition == FL_SERVER_FILE_OVERWRITE_LOCAL &&
        !fl_file_perms_can_overwrite(slot->offer.file_perms))
        return FL_RESULT_ACCES;
    if (disposition == FL_SERVER_FILE_SAVE_TO_SERVER_SHARE &&
        !(slot->offer.file_perms & FL_FILE_PERM_SERVER_SHARE))
        return FL_RESULT_ACCES;
    if (disposition == FL_SERVER_FILE_SAVE_TO_SERVER_SHARE) {
        char saved[512];
        char landed[FL_SERVER_SHARE_ID_MAX + FL_SERVER_FILE_NAME_MAX + 2u];
        rc = fl_server_shared_save_offer(&slot->offer, slot->file_data,
                                         slot->file_len, saved, sizeof(saved));
        if (rc == FL_RESULT_OK &&
            fl_server_shared_landed_basename(&slot->offer, landed,
                                             sizeof(landed)) == FL_RESULT_OK) {
            printf("[Server] saved to server_shared/%s\n", landed);
        }
    } else if (disposition == FL_SERVER_FILE_OVERWRITE_LOCAL) {
        rc = fl_server_shared_overwrite_local(&slot->offer,
                                              slot->offer.suggested_dest_path,
                                              slot->file_data, slot->file_len);
    } else {
        rc = FL_RESULT_OK;
    }
    if (rc != FL_RESULT_OK)
        return rc;
    printf("[Server] accepted %s (%s)\n", share_id,
           disposition == FL_SERVER_FILE_OVERWRITE_LOCAL ? "overwrite" :
           disposition == FL_SERVER_FILE_SAVE_TO_SERVER_SHARE ? "server_shared" :
           "decline");
    share_slot_release(slot);
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_decline(const char *share_id,
                                    uint16_t receiver_id)
{
    fl_server_file_share_slot_t *slot = share_find(share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    if (share_access_ok(&slot->offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    if (slot->offer.receiver_member_id != 0u &&
        slot->offer.receiver_member_id != receiver_id)
        return FL_RESULT_ACCES;
    share_slot_release(slot);
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_revoke(const char *share_id,
                                   uint16_t owner_id)
{
    fl_server_file_share_slot_t *slot = share_find(share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    if (slot->offer.sender_member_id != owner_id)
        return FL_RESULT_ACCES;
    slot->offer.file_perms |= FL_FILE_FLAG_REVOKED;
    share_slot_release(slot);
    return FL_RESULT_OK;
}

fl_result_t fl_server_share_save_to_server_share(const fl_server_file_offer_t *offer)
{
    if (!offer)
        return FL_RESULT_INVAL;
    if (share_access_ok(offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    return fl_server_shared_init();
}

fl_result_t fl_server_share_overwrite_local(const fl_server_file_offer_t *offer,
                                            const char *local_path)
{
    if (!offer || !local_path)
        return FL_RESULT_INVAL;
    if (share_access_ok(offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    if (fl_server_shared_path_is_expired_quarantine(local_path))
        return FL_RESULT_ACCES;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_store_offer(const fl_server_file_offer_t *offer)
{
    if (!offer)
        return FL_RESULT_INVAL;
    if (share_access_ok(offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    if (share_find(offer->share_id) != NULL)
        return FL_RESULT_OK;
    if (share_alloc(offer) == NULL)
        return FL_RESULT_NOMEM;
    (void)fl_server_shared_init();
    return FL_RESULT_OK;
}

fl_result_t fl_net_file_store_chunk(const uint8_t *payload, uint16_t plen)
{
    fl_server_file_chunk_header_t chunk;
    fl_bytes_view_t data;
    fl_server_file_share_slot_t *slot;
    size_t end;
    fl_result_t rc;

    if (!payload)
        return FL_RESULT_INVAL;
    rc = fl_file_packet_decode_chunk(payload, plen, &chunk, &data);
    if (rc != FL_RESULT_OK)
        return rc;
    slot = share_find(chunk.share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    if (share_access_ok(&slot->offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    end = (size_t)chunk.file_offset + (size_t)data.len;
    if (end > FL_SERVER_FILE_MAX_BYTES)
        return FL_RESULT_NOMEM;
    rc = share_slot_ensure_cap(slot, end);
    if (rc != FL_RESULT_OK)
        return rc;
    if (data.len > 0u && data.data)
        memcpy(slot->file_data + chunk.file_offset, data.data, data.len);
    if (end > slot->file_len)
        slot->file_len = end;
    return FL_RESULT_OK;
}

fl_result_t fl_net_file_store_done(const uint8_t *payload, uint16_t plen)
{
    fl_server_file_done_t done;
    fl_server_file_share_slot_t *slot;
    if (!payload)
        return FL_RESULT_INVAL;
    if (fl_file_packet_decode_done(payload, plen, &done) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    slot = share_find(done.share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    if (share_access_ok(&slot->offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    if (done.total_bytes > slot->file_cap ||
        done.total_bytes > (uint64_t)slot->file_len)
        return FL_RESULT_INVAL;
    if (slot->offer.total_chunks != 0u &&
        done.total_chunks != slot->offer.total_chunks)
        return FL_RESULT_INVAL;
    slot->file_len = (size_t)done.total_bytes;
    slot->transfer_complete = 1u;
    if (!slot->meta_received)
        slot->meta_received = 1u;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_meta_from_offer(const fl_server_file_offer_t *offer,
                                           fl_server_file_meta_t *out)
{
    if (!offer || !out || !offer->share_id[0] || !offer->file_name[0])
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));
    copy_str_field(out->share_id, sizeof(out->share_id), offer->share_id);
    out->expires_at = offer->expires_at;
    copy_str_field(out->file_name, sizeof(out->file_name), offer->file_name);
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_meta_encode(const fl_server_file_meta_t *meta,
                                       uint8_t *out,
                                       uint16_t out_cap,
                                       uint16_t *out_len)
{
    fl_bytes_writer_t w;
    fl_result_t rc;
    if (!meta || !out || !out_len)
        return FL_RESULT_INVAL;
    w.buf = out;
    w.cap = out_cap;
    w.len = 0u;
    rc = put_cstring16(&w, meta->share_id);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = put_u64(&w, meta->expires_at);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = put_cstring16(&w, meta->file_name);
    if (rc != FL_RESULT_OK)
        return rc;
    *out_len = w.len;
    return FL_RESULT_OK;
}

fl_result_t fl_server_file_meta_decode(const uint8_t *payload,
                                       uint16_t payload_len,
                                       fl_server_file_meta_t *out)
{
    uint16_t off = 0;
    fl_result_t rc;
    if (!payload || !out)
        return FL_RESULT_INVAL;
    memset(out, 0, sizeof(*out));
    rc = get_cstring16(payload, payload_len, &off, out->share_id, sizeof(out->share_id));
    if (rc != FL_RESULT_OK)
        return rc;
    rc = get_u64(payload, payload_len, &off, &out->expires_at);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = get_cstring16(payload, payload_len, &off, out->file_name, sizeof(out->file_name));
    if (rc != FL_RESULT_OK)
        return rc;
    return FL_RESULT_OK;
}

fl_result_t fl_file_packet_encode_meta(const fl_server_file_meta_t *meta,
                                       uint8_t *out,
                                       uint16_t out_cap,
                                       uint16_t *out_len)
{
    return fl_server_file_meta_encode(meta, out, out_cap, out_len);
}

fl_result_t fl_file_packet_decode_meta(const uint8_t *payload,
                                       uint16_t payload_len,
                                       fl_server_file_meta_t *out)
{
    return fl_server_file_meta_decode(payload, payload_len, out);
}

fl_result_t fl_net_file_store_meta(const uint8_t *payload, uint16_t plen)
{
    fl_server_file_meta_t meta;
    fl_server_file_share_slot_t *slot;
    fl_result_t rc;

    if (!payload)
        return FL_RESULT_INVAL;
    rc = fl_file_packet_decode_meta(payload, plen, &meta);
    if (rc != FL_RESULT_OK)
        return rc;
    slot = share_find(meta.share_id);
    if (!slot)
        return FL_RESULT_NOENT;
    if (share_access_ok(&slot->offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
        return FL_RESULT_ACCES;
    if (meta.file_name[0])
        copy_str_field(slot->offer.file_name, sizeof(slot->offer.file_name),
                       meta.file_name);
    if (meta.expires_at != 0u)
        slot->offer.expires_at = meta.expires_at;
    slot->meta_received = 1u;
    return FL_RESULT_OK;
}

fl_result_t fl_net_file_send_meta(fl_net_server_t *srv,
                                  fl_net_client_t *client,
                                  int hosting,
                                  fl_net_server_member_id_t sender_id,
                                  const fl_server_file_offer_t *offer)
{
    fl_server_file_meta_t meta;
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    fl_result_t rc;

    if (!offer)
        return FL_RESULT_INVAL;
    rc = fl_server_file_meta_from_offer(offer, &meta);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_file_packet_encode_meta(&meta, payload, sizeof(payload), &plen);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_net_file_send_control(srv, client, hosting, sender_id,
                                    FL_NET_SESSION_OP_FILE_META, payload, plen);
}

fl_result_t fl_net_file_send_file_contents(fl_net_server_t *srv,
                                           fl_net_client_t *client,
                                           int hosting,
                                           fl_net_server_member_id_t sender_id,
                                           const fl_server_file_offer_t *offer,
                                           const char *local_path)
{
    FILE *fp;
    uint8_t buf[FL_SERVER_FILE_CHUNK_MAX];
    uint32_t index = 0;
    uint64_t offset = 0;
    fl_result_t rc = FL_RESULT_OK;

    if (!offer || !local_path)
        return FL_RESULT_INVAL;
    rc = fl_net_file_send_meta(srv, client, hosting, sender_id, offer);
    if (rc != FL_RESULT_OK)
        return rc;
    fp = fopen(local_path, "rb");
    if (!fp)
        return FL_RESULT_ERR;
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        fl_server_file_chunk_header_t chunk;
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        if (n == 0u && feof(fp))
            break;
        if (n < sizeof(buf) && ferror(fp)) {
            rc = FL_RESULT_ERR;
            break;
        }
        if (n == 0u)
            break;
        memset(&chunk, 0, sizeof(chunk));
        copy_str_field(chunk.share_id, sizeof(chunk.share_id), offer->share_id);
        chunk.chunk_index = index++;
        chunk.chunk_len = (uint32_t)n;
        chunk.file_offset = offset;
        rc = fl_file_packet_encode_chunk(&chunk, buf, (uint32_t)n, payload,
                                         sizeof(payload), &plen);
        if (rc != FL_RESULT_OK)
            break;
        rc = fl_net_file_send_control(srv, client, hosting, sender_id,
                                      FL_NET_SESSION_OP_FILE_CHUNK, payload, plen);
        if (rc != FL_RESULT_OK)
            break;
        offset += (uint64_t)n;
        if (n < sizeof(buf))
            break;
    }
    if (rc == FL_RESULT_OK) {
        fl_server_file_done_t done;
        uint8_t payload[FL_NET_SESSION_MAX_MSG];
        uint16_t plen = 0;
        memset(&done, 0, sizeof(done));
        copy_str_field(done.share_id, sizeof(done.share_id), offer->share_id);
        done.total_bytes = offset;
        done.total_chunks = index;
        done.status = 1u;
        rc = fl_file_packet_encode_done(&done, payload, sizeof(payload), &plen);
        if (rc == FL_RESULT_OK)
            rc = fl_net_file_send_control(srv, client, hosting, sender_id,
                                          FL_NET_SESSION_OP_FILE_DONE, payload, plen);
    }
    fclose(fp);
    return rc;
}

fl_result_t fl_net_file_send_offer(fl_net_server_t *srv,
                                   fl_net_client_t *client,
                                   int hosting,
                                   fl_net_server_member_id_t sender_id,
                                   const fl_server_file_offer_t *offer)
{
    uint8_t payload[FL_NET_SESSION_MAX_MSG];
    uint16_t plen = 0;
    fl_result_t rc;

    if (!offer)
        return FL_RESULT_INVAL;
    rc = fl_file_packet_encode_offer(offer, payload, sizeof(payload), &plen);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = fl_net_session_validate_file_frame(FL_NET_SESSION_OP_FILE_OFFER, 0u, plen);
    if (rc != FL_RESULT_OK)
        return rc;
    if (hosting) {
        if (!srv)
            return FL_RESULT_INVAL;
        return fl_net_file_host_relay(srv, sender_id, FL_NET_SESSION_OP_FILE_OFFER,
                                      payload, plen);
    }
    if (!client)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle,
                                     (uint8_t)FL_NET_SESSION_OP_FILE_OFFER,
                                     payload, plen);
}

fl_result_t fl_net_file_send_control(fl_net_server_t *srv,
                                     fl_net_client_t *client,
                                     int hosting,
                                     fl_net_server_member_id_t actor_id,
                                     uint8_t opcode,
                                     const uint8_t *payload,
                                     uint16_t plen)
{
    fl_result_t rc;
    (void)actor_id;
    rc = fl_net_session_validate_file_frame(opcode, 0u, plen);
    if (rc != FL_RESULT_OK)
        return rc;
    if (hosting) {
        if (!srv)
            return FL_RESULT_INVAL;
        return fl_net_file_host_relay(srv, actor_id, opcode, payload, plen);
    }
    if (!client)
        return FL_RESULT_INVAL;
    return fl_net_session_send_frame(client->peer_handle, opcode, payload, plen);
}

fl_result_t fl_net_file_host_relay(fl_net_server_t *srv,
                                   fl_net_server_member_id_t sender_id,
                                   uint8_t opcode,
                                   const uint8_t *payload,
                                   uint16_t plen)
{
    fl_server_file_offer_t offer;
    fl_result_t rc;

    if (!srv || !payload)
        return FL_RESULT_INVAL;
    rc = fl_net_session_validate_file_frame(opcode, 0u, plen);
    if (rc != FL_RESULT_OK)
        return rc;

    if (opcode == FL_NET_SESSION_OP_FILE_OFFER) {
        if (fl_file_offer_wire_payload_revoked(payload, plen))
            return FL_RESULT_ACCES;
        rc = fl_file_packet_decode_offer(payload, plen, &offer);
        if (rc != FL_RESULT_OK)
            return rc;
        if (offer.sender_member_id != 0u &&
            offer.sender_member_id != sender_id)
            return FL_RESULT_ACCES;
        offer.sender_member_id = sender_id;
        if (share_access_ok(&offer, (uint64_t)time(NULL)) != FL_RESULT_OK)
            return FL_RESULT_ACCES;
        if (share_find(offer.share_id) == NULL) {
            if (share_alloc(&offer) == NULL)
                return FL_RESULT_NOMEM;
        }
        if (offer.receiver_member_id == 0u) {
            return fl_net_server_broadcast_except(srv, sender_id, opcode, payload, plen);
        }
        return fl_net_server_send_to_member(srv, offer.receiver_member_id, opcode,
                                            payload, plen);
    }

    if (opcode == FL_NET_SESSION_OP_FILE_CHUNK ||
        opcode == FL_NET_SESSION_OP_FILE_DONE ||
        opcode == FL_NET_SESSION_OP_FILE_META) {
        char share_id[FL_SERVER_SHARE_ID_MAX];
        fl_server_file_share_slot_t *slot = NULL;
        if (opcode == FL_NET_SESSION_OP_FILE_CHUNK) {
            fl_server_file_chunk_header_t chunk;
            fl_bytes_view_t data;
            if (fl_file_packet_decode_chunk(payload, plen, &chunk, &data) != FL_RESULT_OK)
                return FL_RESULT_INVAL;
            copy_str_field(share_id, sizeof(share_id), chunk.share_id);
        } else if (opcode == FL_NET_SESSION_OP_FILE_META) {
            fl_server_file_meta_t meta;
            if (fl_file_packet_decode_meta(payload, plen, &meta) != FL_RESULT_OK)
                return FL_RESULT_INVAL;
            copy_str_field(share_id, sizeof(share_id), meta.share_id);
        } else {
            fl_server_file_done_t done;
            if (fl_file_packet_decode_done(payload, plen, &done) != FL_RESULT_OK)
                return FL_RESULT_INVAL;
            copy_str_field(share_id, sizeof(share_id), done.share_id);
        }
        slot = share_find(share_id);
        if (!slot)
            return FL_RESULT_NOENT;
        if (slot->offer.sender_member_id != sender_id)
            return FL_RESULT_ACCES;
        if (slot->offer.receiver_member_id == 0u)
            return fl_net_server_broadcast_except(srv, sender_id, opcode, payload, plen);
        return fl_net_server_send_to_member(srv, slot->offer.receiver_member_id, opcode,
                                            payload, plen);
    }

    if (opcode == FL_NET_SESSION_OP_FILE_ACCEPT ||
        opcode == FL_NET_SESSION_OP_FILE_DECLINE ||
        opcode == FL_NET_SESSION_OP_FILE_REVOKE ||
        opcode == FL_NET_SESSION_OP_FILE_LIST ||
        opcode == FL_NET_SESSION_OP_FILE_STATUS) {
        char share_id[FL_SERVER_SHARE_ID_MAX];
        fl_server_file_share_slot_t *slot = NULL;
        if (opcode == FL_NET_SESSION_OP_FILE_ACCEPT ||
            opcode == FL_NET_SESSION_OP_FILE_DECLINE ||
            opcode == FL_NET_SESSION_OP_FILE_REVOKE) {
            uint16_t actor = 0u;
            if (opcode == FL_NET_SESSION_OP_FILE_REVOKE) {
                if (fl_file_packet_decode_revoke(payload, plen, share_id,
                                                 sizeof(share_id), &actor) != FL_RESULT_OK)
                    return FL_RESULT_INVAL;
                slot = share_find(share_id);
                if (!slot)
                    return FL_RESULT_NOENT;
                if (sender_id != slot->offer.sender_member_id ||
                    (actor != 0u && actor != sender_id))
                    return FL_RESULT_ACCES;
            } else if (opcode == FL_NET_SESSION_OP_FILE_DECLINE) {
                if (fl_file_packet_decode_decline(payload, plen, share_id,
                                                  sizeof(share_id), &actor) != FL_RESULT_OK)
                    return FL_RESULT_INVAL;
                slot = share_find(share_id);
                if (!slot)
                    return FL_RESULT_NOENT;
                if (sender_id == slot->offer.sender_member_id ||
                    (slot->offer.receiver_member_id != 0u &&
                     sender_id != slot->offer.receiver_member_id) ||
                    (actor != 0u && actor != sender_id))
                    return FL_RESULT_ACCES;
            } else {
                fl_server_file_disposition_t disp = FL_SERVER_FILE_DECLINE;
                if (fl_file_packet_decode_accept(payload, plen, share_id, sizeof(share_id),
                                                 &actor, &disp) != FL_RESULT_OK)
                    return FL_RESULT_INVAL;
                slot = share_find(share_id);
                if (!slot)
                    return FL_RESULT_NOENT;
                if (sender_id == slot->offer.sender_member_id ||
                    (slot->offer.receiver_member_id != 0u &&
                     sender_id != slot->offer.receiver_member_id) ||
                    (actor != 0u && actor != sender_id))
                    return FL_RESULT_ACCES;
            }
            return fl_net_server_send_to_member(srv, slot->offer.sender_member_id,
                                                opcode, payload, plen);
        }
        return fl_net_server_broadcast_except(srv, sender_id, opcode, payload, plen);
    }

    return FL_RESULT_INVAL;
}
