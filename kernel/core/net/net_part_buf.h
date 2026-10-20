#ifndef FL_NET_PART_BUF_H
#define FL_NET_PART_BUF_H

/*
 * Base type for a payload that arrives in ordered parts (session frames are 512 bytes, so a
 * file or a sealed letter crosses the wire in pieces). It holds the bytes received so far,
 * accepts the next part only at the offset where the last one ended, and never grows past
 * its limit.
 *
 * Protocols embed it as the first member of their own slot and add their rules around it:
 * server file delivery (net_file_delivery.c: a growing buffer up to the share cap, checked
 * against the offer's size) and KeyQuorum co-signed delivery (net_kq_delivery.c: a letter
 * of known length, uploaded to the host and released to the recipient). The protocol state
 * (offers, co-signatures, release, retry) stays in each protocol; only the buffer is shared.
 *
 * Header-only so every protocol that includes it gets the methods without a new object to
 * link.
 */

#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t   len;   /* bytes received, in order */
    size_t   cap;   /* bytes allocated */
    size_t   limit; /* never more than this many bytes */
} fl_part_buf_t;

/** An empty buffer that will hold at most limit bytes. */
static inline void fl_part_buf_init(fl_part_buf_t *b, size_t limit) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->limit = limit;
}

/** Allocates room for need bytes up front. NOMEM over the limit or when allocation fails. */
static inline fl_result_t fl_part_buf_reserve(fl_part_buf_t *b, size_t need) {
    uint8_t *grown;

    if (need > b->limit)
        return FL_RESULT_NOMEM;
    if (need <= b->cap)
        return FL_RESULT_OK;
    grown = (uint8_t *)realloc(b->data, need ? need : 1u);
    if (!grown)
        return FL_RESULT_NOMEM;
    b->data = grown;
    b->cap = need;
    return FL_RESULT_OK;
}

/**
 * Appends the part that starts at off. INVAL when off is not where the last part ended (a
 * gap, a repeat or a reordering); NOMEM when it would pass the limit or cannot be stored.
 * A part of 0 bytes at the right offset changes nothing.
 */
static inline fl_result_t fl_part_buf_append(fl_part_buf_t *b, uint64_t off, const void *src,
                                             size_t n) {
    fl_result_t rc;

    if (off != (uint64_t)b->len)
        return FL_RESULT_INVAL;
    if (n > b->limit - b->len)
        return FL_RESULT_NOMEM;
    rc = fl_part_buf_reserve(b, b->len + n);
    if (rc != FL_RESULT_OK)
        return rc;
    if (n)
        memcpy(b->data + b->len, src, n);
    b->len += n;
    return FL_RESULT_OK;
}

/** True when every byte up to the limit has arrived (for a payload of known length). */
static inline int fl_part_buf_complete(const fl_part_buf_t *b) {
    return b->data != NULL && b->len == b->limit;
}

/** Frees the bytes and returns the buffer to empty, keeping its limit. */
static inline void fl_part_buf_release(fl_part_buf_t *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

#endif /* FL_NET_PART_BUF_H */
