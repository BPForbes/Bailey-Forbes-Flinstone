/**
 * test_part_buf.c - the base type both server file delivery and KeyQuorum co-signed delivery
 * extend for payloads that arrive in ordered parts (kernel/core/net/net_part_buf.h).
 */
#include "net_part_buf.h"

#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static int test_parts_join_in_order(void) {
    fl_part_buf_t b;

    fl_part_buf_init(&b, 10);
    ASSERT(!fl_part_buf_complete(&b));
    ASSERT(fl_part_buf_append(&b, 0, "abcd", 4) == FL_RESULT_OK);
    ASSERT(fl_part_buf_append(&b, 4, "efg", 3) == FL_RESULT_OK);
    ASSERT(!fl_part_buf_complete(&b));
    ASSERT(fl_part_buf_append(&b, 7, "hij", 3) == FL_RESULT_OK);
    ASSERT(fl_part_buf_complete(&b) && b.len == 10 && memcmp(b.data, "abcdefghij", 10) == 0);
    fl_part_buf_release(&b);
    ASSERT(b.data == NULL && b.len == 0 && b.cap == 0 && b.limit == 10);
    return 0;
}

static int test_gaps_repeats_and_reorders_are_refused(void) {
    fl_part_buf_t b;

    fl_part_buf_init(&b, 16);
    ASSERT(fl_part_buf_append(&b, 2, "xx", 2) == FL_RESULT_INVAL);  /* gap */
    ASSERT(fl_part_buf_append(&b, 0, "abcd", 4) == FL_RESULT_OK);
    ASSERT(fl_part_buf_append(&b, 0, "abcd", 4) == FL_RESULT_INVAL); /* repeat */
    ASSERT(fl_part_buf_append(&b, 2, "zz", 2) == FL_RESULT_INVAL);   /* overlap */
    ASSERT(b.len == 4 && memcmp(b.data, "abcd", 4) == 0);
    /* A part of 0 bytes at the right offset changes nothing. */
    ASSERT(fl_part_buf_append(&b, 4, NULL, 0) == FL_RESULT_OK && b.len == 4);
    fl_part_buf_release(&b);
    return 0;
}

static int test_the_limit_is_never_passed(void) {
    fl_part_buf_t b;

    fl_part_buf_init(&b, 5);
    ASSERT(fl_part_buf_reserve(&b, 6) == FL_RESULT_NOMEM);
    ASSERT(fl_part_buf_reserve(&b, 5) == FL_RESULT_OK && b.cap == 5 && b.len == 0);
    ASSERT(fl_part_buf_append(&b, 0, "abc", 3) == FL_RESULT_OK);
    ASSERT(fl_part_buf_append(&b, 3, "def", 3) == FL_RESULT_NOMEM);
    ASSERT(b.len == 3 && b.cap == 5);
    ASSERT(fl_part_buf_append(&b, 3, "de", 2) == FL_RESULT_OK && fl_part_buf_complete(&b));
    fl_part_buf_release(&b);
    return 0;
}

int main(void) {
    int rc = 0;
#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_parts_join_in_order);
    RUN(test_gaps_repeats_and_reorders_are_refused);
    RUN(test_the_limit_is_never_passed);
    if (rc == 0)
        printf("All part buffer tests passed.\n");
    return rc;
}
