/**
 * PQ invariant tests: FIFO tie-breaking, stable ordering, determinism.
 */
#include "priority_queue.h"
#include <stdio.h>
#include <string.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int order[16];
static int order_cnt;

static void recorder(void *arg) {
    int v = *(int *)arg;
    order[order_cnt++] = v;
}

static int test_fifo_tiebreak(void) {
    priority_queue_t pq;
    pq_init(&pq);
    order_cnt = 0;
    int a = 1, b = 2, c = 3;
    pq_push(&pq, 1, recorder, &a);  /* priority 1, seq 0 */
    pq_push(&pq, 1, recorder, &b);  /* priority 1, seq 1 */
    pq_push(&pq, 1, recorder, &c);  /* priority 1, seq 2 */
    pq_task_t t;
    int r = pq_pop(&pq, &t);
    ASSERT(r == 0 && t.arg == &a);
    r = pq_pop(&pq, &t);
    ASSERT(r == 0 && t.arg == &b);
    r = pq_pop(&pq, &t);
    ASSERT(r == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_priority_ordering(void) {
    priority_queue_t pq;
    pq_init(&pq);
    order_cnt = 0;
    int v0 = 0, v1 = 1, v2 = 2;
    pq_push(&pq, 2, recorder, &v2);
    pq_push(&pq, 0, recorder, &v0);
    pq_push(&pq, 1, recorder, &v1);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &v0);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &v1);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &v2);
    return 0;
}

static int test_stable_repeated(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int vals[8];
    for (int i = 0; i < 8; i++) vals[i] = i;
    for (int r = 0; r < 2; r++) {
        for (int i = 0; i < 4; i++)
            pq_push(&pq, 0, recorder, &vals[i]);
        pq_task_t t;
        for (int i = 0; i < 4; i++) {
            ASSERT(pq_pop(&pq, &t) == 0);
            ASSERT(*(int *)t.arg == i);
        }
    }
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_update_priority(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1, b = 2, c = 3;
    pq_push(&pq, 2, recorder, &a);  /* a at priority 2 */
    pq_handle_t hb = pq_push(&pq, 2, recorder, &b);
    pq_push(&pq, 2, recorder, &c);
    ASSERT(hb >= 0);
    /* Boost b from 2 to 0 (highest) - should pop before a,c */
    ASSERT(pq_update(&pq, hb, 0) == 0);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &b);  /* b boosted to top */
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

int main(void) {
    printf("test_fifo_tiebreak... ");
    if (test_fifo_tiebreak() != 0) return 1;
    printf("OK\n");
    printf("test_priority_ordering... ");
    if (test_priority_ordering() != 0) return 1;
    printf("OK\n");
    printf("test_stable_repeated... ");
    if (test_stable_repeated() != 0) return 1;
    printf("OK\n");
    printf("test_update_priority... ");
    if (test_update_priority() != 0) return 1;
    printf("OK\n");
    printf("All PQ tests passed.\n");
    return 0;
}
