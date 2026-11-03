/* History bundle framing (FL-04): round trip, truncation at every length, trailing bytes, wrong
 * count, empty and oversize letters, a length field that overflows. Framing only: the letters
 * here are arbitrary bytes. */
#include "kq_history_bundle.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int test_round_trip(void)
{
    static uint8_t buf[64];
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {9, 8, 7, 6, 5};
    const uint8_t *p15 = NULL, *p17 = NULL;
    size_t len = 0, n15 = 0, n17 = 0;

    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, sizeof a, b, sizeof b) == FL_RESULT_OK);
    ASSERT(len == 1u + 4u + sizeof a + 4u + sizeof b);
    ASSERT(buf[0] == 2 && buf[1] == 0 && buf[4] == 3 && buf[5] == 1);
    ASSERT(fl_history_bundle_unpack(buf, len, &p15, &n15, &p17, &n17) == FL_RESULT_OK);
    ASSERT(n15 == sizeof a && memcmp(p15, a, n15) == 0);
    ASSERT(n17 == sizeof b && memcmp(p17, b, n17) == 0);
    ASSERT(p15 >= buf && p15 + n15 <= buf + len); /* views, not copies */
    return 0;
}

static int test_every_truncation_is_refused(void)
{
    static uint8_t buf[32];
    const uint8_t a[] = {1, 2, 3};
    const uint8_t b[] = {4, 5};
    const uint8_t *p15 = (const uint8_t *)buf, *p17 = (const uint8_t *)buf;
    size_t len = 0, n15 = 77, n17 = 88;

    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, sizeof a, b, sizeof b) == FL_RESULT_OK);
    for (size_t cut = 0; cut < len; cut++)
        ASSERT(fl_history_bundle_unpack(buf, cut, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    ASSERT(n15 == 77 && n17 == 88 && p15 == buf && p17 == buf); /* untouched on failure */
    return 0;
}

static int test_trailing_byte_is_refused(void)
{
    static uint8_t buf[32];
    const uint8_t a[] = {1}, b[] = {2};
    const uint8_t *p15, *p17;
    size_t len = 0, n15, n17;

    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, 1, b, 1) == FL_RESULT_OK);
    buf[len] = 0;
    ASSERT(fl_history_bundle_unpack(buf, len + 1, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    return 0;
}

static int test_wrong_count_is_refused(void)
{
    static uint8_t buf[32];
    const uint8_t a[] = {1}, b[] = {2};
    const uint8_t *p15, *p17;
    size_t len = 0, n15, n17;

    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, 1, b, 1) == FL_RESULT_OK);
    for (unsigned c = 0; c < 256; c++) {
        buf[0] = (uint8_t)c;
        ASSERT((fl_history_bundle_unpack(buf, len, &p15, &n15, &p17, &n17) == FL_RESULT_OK) == (c == 2));
    }
    return 0;
}

static int test_empty_and_overlong_letters(void)
{
    static uint8_t buf[32];
    const uint8_t a[] = {1}, b[] = {2};
    const uint8_t *p15, *p17;
    size_t len = 0, n15, n17;

    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, 0, b, 1) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, a, 1, b, 0) == FL_RESULT_INVAL);
    /* two zero length fields: exactly 9 bytes, so only the empty-letter check refuses it */
    memset(buf, 0, sizeof buf);
    buf[0] = 2;
    ASSERT(fl_history_bundle_unpack(buf, 9, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    /* a length field of 0xFFFFFFFF must not wrap the bounds check */
    memset(buf, 0, sizeof buf);
    buf[0] = 2;
    memset(buf + 1, 0xFF, 4);
    ASSERT(fl_history_bundle_unpack(buf, sizeof buf, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    /* first letter swallows everything, leaving no room for the second */
    memset(buf, 0, sizeof buf);
    buf[0] = 2;
    buf[4] = 27; /* 1 + 4 + 27 == 32 */
    ASSERT(fl_history_bundle_unpack(buf, sizeof buf, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    return 0;
}

static int test_bad_arguments_and_capacity(void)
{
    static uint8_t buf[16];
    const uint8_t a[] = {1, 2, 3}, b[] = {4};
    const uint8_t *p15, *p17;
    size_t len = 0, n15, n17;

    ASSERT(fl_history_bundle_pack(NULL, sizeof buf, &len, a, 3, b, 1) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_pack(buf, sizeof buf, NULL, a, 3, b, 1) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_pack(buf, sizeof buf, &len, NULL, 3, b, 1) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_pack(buf, 12, &len, a, 3, b, 1) == FL_RESULT_NOMEM); /* needs 13 */
    ASSERT(fl_history_bundle_pack(buf, 13, &len, a, 3, b, 1) == FL_RESULT_OK && len == 13);
    ASSERT(fl_history_bundle_unpack(NULL, 13, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_unpack(buf, 13, NULL, &n15, &p17, &n17) == FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_unpack(buf, 0, &p15, &n15, &p17, &n17) == FL_RESULT_INVAL);
    return 0;
}

static int test_oversize_is_refused(void)
{
    /* The bound is checked before any copy; a 1 MiB + 1 letter never needs a real buffer. */
    static uint8_t big[FL_HISTORY_BUNDLE_MAX_BYTES + 16];
    static uint8_t src[FL_HISTORY_BUNDLE_MAX_BYTES + 16];
    const uint8_t a[] = {1};
    const uint8_t *p15, *p17;
    size_t len = 0, n15, n17;

    ASSERT(fl_history_bundle_pack(big, sizeof big, &len, src, FL_HISTORY_BUNDLE_MAX_BYTES, a, 1) ==
           FL_RESULT_INVAL);
    ASSERT(fl_history_bundle_pack(big, sizeof big, &len, src, FL_HISTORY_BUNDLE_MAX_BYTES - 9, a, 1) ==
           FL_RESULT_INVAL); /* + framing > max */
    ASSERT(fl_history_bundle_pack(big, sizeof big, &len, src, FL_HISTORY_BUNDLE_MAX_BYTES - 10 - 1, a, 1) ==
           FL_RESULT_OK && len == FL_HISTORY_BUNDLE_MAX_BYTES - 10 - 1 + 10);
    ASSERT(fl_history_bundle_unpack(big, FL_HISTORY_BUNDLE_MAX_BYTES + 1, &p15, &n15, &p17, &n17) ==
           FL_RESULT_INVAL);
    return 0;
}

int main(void)
{
    if (test_round_trip() || test_every_truncation_is_refused() || test_trailing_byte_is_refused() ||
        test_wrong_count_is_refused() || test_empty_and_overlong_letters() ||
        test_bad_arguments_and_capacity() || test_oversize_is_refused())
        return 1;
    printf("All KeyQuorum history bundle tests passed.\n");
    return 0;
}
