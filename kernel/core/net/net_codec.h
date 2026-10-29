/*
 * net_codec.h -- the one bounded big-endian byte codec for network wire formats.
 *
 * Writer side: fl_bytes_writer_t (contract_p5_file_delivery.h). Every put checks the room
 * first and writes nothing on overflow (FL_RESULT_NOMEM).
 * Reader side: either the (buf, cap, *off) form, which advances *off only on success, or
 * fl_bytes_reader_t, a cursor with a sticky `bad` flag built on the same primitives. A short
 * read is FL_RESULT_INVAL.
 *
 * Integers go through net_endian.h (fl_net_put_*_be / fl_net_get_*_be), so the bytes are the
 * same with or without FL_NET_ASM_AVAILABLE. Bounds are compared as size_t, so the checks
 * cannot wrap in uint16_t.
 *
 * Naming: fl_codec_* are the stateless forms (writer, or buf + cap + offset pointer);
 * fl_bytes_reader_* is the stateful cursor type and its methods. The prefix names the object.
 *
 * Strings: fl_codec_put_str8 / fl_bytes_reader_read_str8 use a one-byte length prefix (at
 * most 255 bytes); the file-delivery u16-prefixed form stays fl_wire_put_bytes16 /
 * fl_wire_get_bytes16. They are different wire formats and are not interchangeable.
 */
#ifndef FL_NET_CODEC_H
#define FL_NET_CODEC_H

#include "contract_p5_file_delivery.h"
#include "net_endian.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- writer --------------------------------------------------------------- */

static inline fl_result_t fl_codec_put_bytes(fl_bytes_writer_t *w, const void *src, size_t n)
{
    if (!w || w->len > w->cap || n > (size_t)(w->cap - w->len))
        return FL_RESULT_NOMEM;
    if (n && !src)
        return FL_RESULT_INVAL;
    /* Nonzero length into a null buffer is INVAL; len stays put. n == 0 is a no-op. */
    if (n && !w->buf)
        return FL_RESULT_INVAL;
    if (n)
        memcpy(w->buf + w->len, src, n);
    w->len = (uint16_t)(w->len + n);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_codec_put_u8(fl_bytes_writer_t *w, uint8_t v)
{
    return fl_codec_put_bytes(w, &v, 1u);
}

static inline fl_result_t fl_codec_put_u16(fl_bytes_writer_t *w, uint16_t v)
{
    uint8_t b[2];

    fl_net_put_u16_be(b, v);
    return fl_codec_put_bytes(w, b, sizeof b);
}

static inline fl_result_t fl_codec_put_u32(fl_bytes_writer_t *w, uint32_t v)
{
    uint8_t b[4];

    fl_net_put_u32_be(b, v);
    return fl_codec_put_bytes(w, b, sizeof b);
}

static inline fl_result_t fl_codec_put_u64(fl_bytes_writer_t *w, uint64_t v)
{
    uint8_t b[8];

    fl_net_put_u64_be(b, v);
    return fl_codec_put_bytes(w, b, sizeof b);
}

/** One length byte, then the string bytes (NUL not written). NULL is the empty string.
 *  Longer than 255 bytes is INVAL; no room is NOMEM and nothing is written.
 *  If the length byte fails, that result is returned and the payload is not written. */
static inline fl_result_t fl_codec_put_str8(fl_bytes_writer_t *w, const char *s)
{
    size_t len = s ? strlen(s) : 0u;
    fl_result_t rc;

    if (len > 255u)
        return FL_RESULT_INVAL;
    if (!w || w->len > w->cap || 1u + len > (size_t)(w->cap - w->len))
        return FL_RESULT_NOMEM;
    rc = fl_codec_put_u8(w, (uint8_t)len);
    if (rc != FL_RESULT_OK)
        return rc;
    return fl_codec_put_bytes(w, s, len);
}

/* ---- reader, (buf, cap, *off) form ---------------------------------------- */

/** Borrows n bytes at *off from buf (no copy), advancing *off only on success. The one
 *  bounds check every reader below is built on. n == 0 yields a NULL view. */
static inline fl_result_t fl_codec_get_view(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                            size_t n, fl_bytes_view_t *out)
{
    if (!buf || !off || !out || *off > cap || n > (size_t)(cap - *off))
        return FL_RESULT_INVAL;
    out->data = n ? buf + *off : NULL;
    out->len = (uint16_t)n;
    *off = (uint16_t)(*off + n);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_codec_get_bytes(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                             void *dst, size_t n)
{
    fl_bytes_view_t v;

    if (!off || (n && !dst))
        return FL_RESULT_INVAL;
    if (fl_codec_get_view(buf, cap, off, n, &v) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (n)
        memcpy(dst, v.data, n);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_codec_get_u8(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                          uint8_t *out)
{
    return out ? fl_codec_get_bytes(buf, cap, off, out, 1u) : FL_RESULT_INVAL;
}

static inline fl_result_t fl_codec_get_u16(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                           uint16_t *out)
{
    uint8_t b[2];

    if (!out || fl_codec_get_bytes(buf, cap, off, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u16_be(b);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_codec_get_u32(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                           uint32_t *out)
{
    uint8_t b[4];

    if (!out || fl_codec_get_bytes(buf, cap, off, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u32_be(b);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_codec_get_u64(const uint8_t *buf, uint16_t cap, uint16_t *off,
                                           uint64_t *out)
{
    uint8_t b[8];

    if (!out || fl_codec_get_bytes(buf, cap, off, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u64_be(b);
    return FL_RESULT_OK;
}

/* ---- reader, cursor with a sticky failure flag ----------------------------- */

typedef struct fl_bytes_reader {
    const uint8_t *buf;
    uint16_t cap;
    uint16_t off;
    int bad; /* set by the first failed read; every later read then fails */
} fl_bytes_reader_t;

static inline void fl_bytes_reader_init(fl_bytes_reader_t *r, const uint8_t *buf, uint16_t cap)
{
    if (!r)
        return;
    r->buf = buf;
    r->cap = cap;
    r->off = 0u;
    r->bad = 0;
}

static inline uint16_t fl_bytes_reader_remaining(const fl_bytes_reader_t *r)
{
    return (r && r->off <= r->cap) ? (uint16_t)(r->cap - r->off) : 0u;
}

static inline const uint8_t *fl_bytes_reader_cursor(const fl_bytes_reader_t *r)
{
    return (r && r->buf) ? r->buf + r->off : NULL;
}

static inline fl_result_t fl_bytes_reader_read_bytes(fl_bytes_reader_t *r, void *dst, size_t n)
{
    if (!r)
        return FL_RESULT_INVAL;
    if (r->bad || fl_codec_get_bytes(r->buf, r->cap, &r->off, dst, n) != FL_RESULT_OK) {
        r->bad = 1;
        return FL_RESULT_INVAL;
    }
    return FL_RESULT_OK;
}

static inline fl_result_t fl_bytes_reader_read_u8(fl_bytes_reader_t *r, uint8_t *out)
{
    return fl_bytes_reader_read_bytes(r, out, 1u);
}

static inline fl_result_t fl_bytes_reader_read_u16(fl_bytes_reader_t *r, uint16_t *out)
{
    uint8_t b[2];

    if (!out)
        return fl_bytes_reader_read_bytes(r, NULL, sizeof b);
    if (fl_bytes_reader_read_bytes(r, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u16_be(b);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_bytes_reader_read_u32(fl_bytes_reader_t *r, uint32_t *out)
{
    uint8_t b[4];

    if (!out)
        return fl_bytes_reader_read_bytes(r, NULL, sizeof b);
    if (fl_bytes_reader_read_bytes(r, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u32_be(b);
    return FL_RESULT_OK;
}

static inline fl_result_t fl_bytes_reader_read_u64(fl_bytes_reader_t *r, uint64_t *out)
{
    uint8_t b[8];

    if (!out)
        return fl_bytes_reader_read_bytes(r, NULL, sizeof b);
    if (fl_bytes_reader_read_bytes(r, b, sizeof b) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    *out = fl_net_get_u64_be(b);
    return FL_RESULT_OK;
}

/** Reads a one-byte-prefixed string into out[cap] and terminates it. A length of cap or
 *  more, a short buffer or an embedded NUL fails (INVAL, `bad`, out[0] = '\0').
 *  A null reader is INVAL and does not touch out. A null out is a failed one-byte read. */
static inline fl_result_t fl_bytes_reader_read_str8(fl_bytes_reader_t *r, char *out, size_t cap)
{
    uint8_t l = 0u;

    if (!r)
        return FL_RESULT_INVAL;
    if (!out)
        return fl_bytes_reader_read_bytes(r, NULL, 1u);
    if (fl_bytes_reader_read_u8(r, &l) != FL_RESULT_OK || (size_t)l >= cap ||
        l > fl_bytes_reader_remaining(r) || (l && memchr(fl_bytes_reader_cursor(r), 0, l))) {
        r->bad = 1;
        if (cap)
            out[0] = '\0';
        return FL_RESULT_INVAL;
    }
    (void)fl_bytes_reader_read_bytes(r, out, l);
    out[l] = '\0';
    return FL_RESULT_OK;
}

/* ---- sticky writer ---------------------------------------------------------- */

/** The write side of fl_bytes_reader_t: an fl_bytes_writer_t that stays failed after the
 *  first failed put, so a frame can be built with a run of puts and checked once. After a
 *  failure every later put writes nothing and returns INVAL. */
typedef struct fl_bytes_sticky_writer {
    fl_bytes_writer_t w;
    int bad; /* set by the first failed put */
} fl_bytes_sticky_writer_t;

static inline void fl_bytes_sticky_init(fl_bytes_sticky_writer_t *s, uint8_t *buf, uint16_t cap)
{
    fl_bytes_writer_init(&s->w, buf, cap);
    s->bad = 0;
}

static inline fl_result_t fl_bytes_sticky_note(fl_bytes_sticky_writer_t *s, fl_result_t rc)
{
    if (rc != FL_RESULT_OK)
        s->bad = 1;
    return rc;
}

static inline fl_result_t fl_bytes_sticky_put_bytes(fl_bytes_sticky_writer_t *s, const void *src,
                                                    size_t n)
{
    return s->bad ? FL_RESULT_INVAL : fl_bytes_sticky_note(s, fl_codec_put_bytes(&s->w, src, n));
}

static inline fl_result_t fl_bytes_sticky_put_u32(fl_bytes_sticky_writer_t *s, uint32_t v)
{
    return s->bad ? FL_RESULT_INVAL : fl_bytes_sticky_note(s, fl_codec_put_u32(&s->w, v));
}

static inline fl_result_t fl_bytes_sticky_put_u64(fl_bytes_sticky_writer_t *s, uint64_t v)
{
    return s->bad ? FL_RESULT_INVAL : fl_bytes_sticky_note(s, fl_codec_put_u64(&s->w, v));
}

static inline fl_result_t fl_bytes_sticky_put_str8(fl_bytes_sticky_writer_t *s, const char *str)
{
    return s->bad ? FL_RESULT_INVAL : fl_bytes_sticky_note(s, fl_codec_put_str8(&s->w, str));
}

#endif
