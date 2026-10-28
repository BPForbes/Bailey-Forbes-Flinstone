/**
 * test_hex.c - the shared hex codec (kernel/include/fl/hex.h) and the
 * KeyQuorum delivery id wrappers that sit on it.
 */
#include "fl/hex.h"
#include "kq_delivery.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static int test_encode_is_lower_case_and_terminated(void) {
    const uint8_t in[] = {0x00, 0x0f, 0xa5, 0xff};
    char out[2 * sizeof in + 1];

    memset(out, 'x', sizeof out);
    fl_hex_encode(in, sizeof in, out);
    ASSERT(strcmp(out, "000fa5ff") == 0);
    fl_hex_encode(in, 0, out);
    ASSERT(out[0] == '\0');
    return 0;
}

static int test_decode_round_trip_and_case(void) {
    uint8_t out[4];
    const uint8_t want[] = {0x00, 0x0f, 0xa5, 0xff};

    ASSERT(fl_hex_decode("000fa5ff", out, sizeof out) == 0);
    ASSERT(memcmp(out, want, sizeof want) == 0);
    ASSERT(fl_hex_decode("000FA5FF", out, sizeof out) == 0);
    ASSERT(memcmp(out, want, sizeof want) == 0);
    ASSERT(fl_hex_decode("", out, 0) == 0);
    return 0;
}

static int test_decode_refuses_bad_input(void) {
    uint8_t out[4];

    ASSERT(fl_hex_decode(NULL, out, sizeof out) == -1);
    ASSERT(fl_hex_decode("000fa5f", out, sizeof out) == -1);   /* short */
    ASSERT(fl_hex_decode("000fa5ff00", out, sizeof out) == -1); /* long */
    ASSERT(fl_hex_decode("000fa5fg", out, sizeof out) == -1);  /* not hex */
    ASSERT(fl_hex_decode("000fa5f ", out, sizeof out) == -1);
    ASSERT(fl_hex_nibble('/') == -1 && fl_hex_nibble('G') == -1 && fl_hex_nibble('F') == 15);
    return 0;
}

static int test_decode_refuses_wrapping_len(void) {
    uint8_t out[4];

    /* 2 * len wraps to 0 (SIZE_MAX/2 + 1) or to a small value: both must be refused,
     * not matched against an empty string. */
    ASSERT(fl_hex_decode("", out, SIZE_MAX / 2 + 1) == -1);
    ASSERT(fl_hex_decode("", out, SIZE_MAX / 2) == -1);
    ASSERT(fl_hex_decode("00", out, SIZE_MAX / 2 + 2) == -1);
    ASSERT(fl_hex_decode("", out, 0) == 0);
    return 0;
}

static int test_delivery_id_wrappers(void) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t back[FL_KQ_DELIVERY_ID_LEN];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    for (size_t i = 0; i < sizeof id; i++)
        id[i] = (uint8_t)(i * 37 + 1);
    fl_kq_delivery_id_hex(id, hex);
    ASSERT(strlen(hex) == 2 * FL_KQ_DELIVERY_ID_LEN);
    ASSERT(fl_kq_delivery_id_parse(hex, back) == 0);
    ASSERT(memcmp(id, back, sizeof id) == 0);
    ASSERT(fl_kq_delivery_id_parse("zz", back) == -1);
    return 0;
}

int main(void) {
    if (test_encode_is_lower_case_and_terminated() || test_decode_round_trip_and_case() ||
        test_decode_refuses_bad_input() || test_decode_refuses_wrapping_len() ||
        test_delivery_id_wrappers())
        return 1;
    printf("test_hex: all passed\n");
    return 0;
}
