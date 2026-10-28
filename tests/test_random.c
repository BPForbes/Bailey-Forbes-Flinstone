/**
 * test_random.c - the shared random source (kernel/include/fl/random.h).
 * No fixed seeds and no secrets: it checks length, bounds, failure behaviour
 * and that two draws differ.
 */
#include "fl/random.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define SENTINEL 0xAA

static int all_equal(const uint8_t *p, size_t n, uint8_t v) {
    for (size_t i = 0; i < n; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

static int test_fills_exactly_len(void) {
    uint8_t buf[64];
    static const size_t lens[] = {1, 2, 16, 32, 63};

    for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        size_t n = lens[k];

        memset(buf, SENTINEL, sizeof buf);
        ASSERT(fl_random_bytes(buf, n) == 0);
        ASSERT(all_equal(buf + n, sizeof buf - n, SENTINEL)); /* no overrun */
    }
    return 0;
}

static int test_two_draws_differ(void) {
    uint8_t a[32], b[32], zero[32];

    memset(zero, 0, sizeof zero);
    ASSERT(fl_random_bytes(a, sizeof a) == 0);
    ASSERT(fl_random_bytes(b, sizeof b) == 0);
    ASSERT(memcmp(a, b, sizeof a) != 0);
    ASSERT(memcmp(a, zero, sizeof a) != 0);
    return 0;
}

static int test_large_draw_is_filled(void) {
    static uint8_t buf[4096];

    memset(buf, 0, sizeof buf);
    ASSERT(fl_random_bytes(buf, sizeof buf) == 0);
    for (size_t off = 0; off < sizeof buf; off += 64)
        ASSERT(!all_equal(buf + off, 64, 0)); /* every 64-byte block was written */
    return 0;
}

static int test_refuses_bad_arguments(void) {
    uint8_t buf[16];

    errno = 0;
    ASSERT(fl_random_bytes(NULL, 16) == -1 && errno == EINVAL);
    memset(buf, SENTINEL, sizeof buf);
    errno = 0;
    ASSERT(fl_random_bytes(buf, 0) == -1 && errno == EINVAL);
    ASSERT(all_equal(buf, sizeof buf, SENTINEL)); /* nothing touched */
    errno = 0;
    ASSERT(fl_random_bytes(buf, SIZE_MAX) == -1 && errno == EINVAL);
    errno = 0;
    ASSERT(fl_random_bytes(buf, (size_t)INT_MAX + 1u) == -1 && errno == EINVAL);
    ASSERT(all_equal(buf, sizeof buf, SENTINEL)); /* refused before any write or wipe */
    return 0;
}

int main(void) {
    if (test_fills_exactly_len() || test_two_draws_differ() || test_large_draw_is_filled() ||
        test_refuses_bad_arguments())
        return 1;
    printf("test_random: all passed\n");
    return 0;
}
