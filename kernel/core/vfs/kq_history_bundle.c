#include "kq_history_bundle.h"

#include "net_endian.h"

#include <string.h>

#define BUNDLE_HEAD 1u
#define LEN_FIELD 4u

fl_result_t fl_history_bundle_pack(uint8_t *out, size_t cap, size_t *out_len,
                                   const uint8_t *l15, size_t n15, const uint8_t *l17, size_t n17)
{
    size_t total;

    if (!out || !out_len || !l15 || !l17 || n15 == 0 || n17 == 0)
        return FL_RESULT_INVAL;
    /* Each length is checked on its own first, so the sum cannot wrap. */
    if (n15 > FL_HISTORY_BUNDLE_MAX_BYTES || n17 > FL_HISTORY_BUNDLE_MAX_BYTES)
        return FL_RESULT_INVAL;
    total = BUNDLE_HEAD + LEN_FIELD + n15 + LEN_FIELD + n17;
    if (total > FL_HISTORY_BUNDLE_MAX_BYTES)
        return FL_RESULT_INVAL;
    if (total > cap)
        return FL_RESULT_NOMEM;

    out[0] = (uint8_t)FL_HISTORY_BUNDLE_MAX_LETTERS;
    fl_net_put_u32_be(out + BUNDLE_HEAD, (uint32_t)n15);
    memcpy(out + BUNDLE_HEAD + LEN_FIELD, l15, n15);
    fl_net_put_u32_be(out + BUNDLE_HEAD + LEN_FIELD + n15, (uint32_t)n17);
    memcpy(out + BUNDLE_HEAD + LEN_FIELD + n15 + LEN_FIELD, l17, n17);
    *out_len = total;
    return FL_RESULT_OK;
}

/* One letter at *off: reads its length, bounds it by what is left, returns a view. */
static fl_result_t take_letter(const uint8_t *in, size_t len, size_t *off, const uint8_t **p,
                               size_t *n)
{
    size_t left, want;

    if (*off > len || len - *off < LEN_FIELD)
        return FL_RESULT_INVAL;
    want = fl_net_get_u32_be(in + *off);
    *off += LEN_FIELD;
    left = len - *off;
    if (want == 0 || want > left)
        return FL_RESULT_INVAL;
    *p = in + *off;
    *n = want;
    *off += want;
    return FL_RESULT_OK;
}

fl_result_t fl_history_bundle_unpack(const uint8_t *in, size_t len, const uint8_t **l15, size_t *n15,
                                     const uint8_t **l17, size_t *n17)
{
    size_t off = BUNDLE_HEAD;
    const uint8_t *p15 = NULL, *p17 = NULL;
    size_t m15 = 0, m17 = 0;

    if (!in || !l15 || !n15 || !l17 || !n17 || len < BUNDLE_HEAD || len > FL_HISTORY_BUNDLE_MAX_BYTES)
        return FL_RESULT_INVAL;
    if (in[0] != FL_HISTORY_BUNDLE_MAX_LETTERS)
        return FL_RESULT_INVAL;
    if (take_letter(in, len, &off, &p15, &m15) != FL_RESULT_OK ||
        take_letter(in, len, &off, &p17, &m17) != FL_RESULT_OK)
        return FL_RESULT_INVAL;
    if (off != len)
        return FL_RESULT_INVAL; /* trailing bytes */
    *l15 = p15;
    *n15 = m15;
    *l17 = p17;
    *n17 = m17;
    return FL_RESULT_OK;
}
