/**
 * PQ/MLQ test suite: invariants, scheduler-grade features, edge cases.
 */
#include "priority_queue.h"
#include <stdio.h>
#include <string.h>

#define RUN_TEST(name) do { \
    printf("%s... ", #name); \
    if (test_##name() != 0) { fprintf(stderr, "FAIL\n"); return 1; } \
    printf("OK\n"); \
} while(0)

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", #c); return 1; } } while(0)

static int order[64];
static int order_cnt;

static void recorder(void *arg) {
    int v = *(int *)arg;
    if (order_cnt < 64) order[order_cnt++] = v;
}

/* --- Invariants --- */
static int test_fifo_tiebreak(void) {
    priority_queue_t pq;
    pq_init(&pq);
    order_cnt = 0;
    int a = 1, b = 2, c = 3;
    pq_push(&pq, 1, recorder, &a);
    pq_push(&pq, 1, recorder, &b);
    pq_push(&pq, 1, recorder, &c);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &b);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_priority_ordering(void) {
    priority_queue_t pq;
    pq_init(&pq);
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
    pq_push(&pq, 2, recorder, &a);
    pq_handle_t hb = pq_push(&pq, 2, recorder, &b);
    pq_push(&pq, 2, recorder, &c);
    ASSERT(hb >= 0);
    ASSERT(pq_update(&pq, hb, 0) == 0);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &b);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_layer_scan(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1, b = 2, c = 3, d = 4;
    pq_push(&pq, 1, recorder, &a);
    pq_push(&pq, 0, recorder, &b);
    pq_push(&pq, 1, recorder, &c);
    pq_push(&pq, 0, recorder, &d);
    ASSERT(pq_has_layer(&pq, 0) && pq_has_layer(&pq, 1));
    ASSERT(pq_count_layer(&pq, 0) == 2);
    ASSERT(pq_count_layer(&pq, 1) == 2);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &b);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &d);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_pop_from_layer(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1, b = 2, c = 3;
    pq_push(&pq, 0, recorder, &a);
    pq_push(&pq, 1, recorder, &b);
    pq_push(&pq, 0, recorder, &c);
    pq_task_t t;
    ASSERT(pq_pop_from_layer(&pq, 1, &t) == 0 && t.arg == &b);
    ASSERT(pq_pop_from_layer(&pq, 1, &t) == -1);
    ASSERT(pq_pop_from_layer(&pq, 0, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop_from_layer(&pq, 0, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    return 0;
}

static int test_remove(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1, b = 2, c = 3;
    pq_handle_t ha = pq_push(&pq, 1, recorder, &a);
    pq_handle_t hb = pq_push(&pq, 1, recorder, &b);
    pq_push(&pq, 1, recorder, &c);
    ASSERT(ha >= 0 && hb >= 0);
    ASSERT(pq_remove(&pq, hb) == 0);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &c);
    ASSERT(pq_is_empty(&pq));
    ASSERT(pq_remove(&pq, hb) == -1);
    return 0;
}

static int test_stale_handle(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1;
    pq_handle_t h = pq_push(&pq, 0, recorder, &a);
    ASSERT(h >= 0);
    pq_task_t t;
    pq_pop(&pq, &t);
    ASSERT(pq_update(&pq, h, 1) == -1);
    ASSERT(pq_remove(&pq, h) == -1);
    return 0;
}

/* --- Scheduler-grade --- */
static int test_set_quantum(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1;
    pq_handle_t h = pq_push(&pq, 0, recorder, &a);
    ASSERT(h >= 0);
    pq_set_quantum(&pq, h, 100);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0 && t.arg == &a);
    ASSERT(t.quantum == 100);
    ASSERT(t.runtime == 0);
    return 0;
}

static int test_quantum_runtime_defaults(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int a = 1;
    pq_push(&pq, 0, recorder, &a);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0);
    ASSERT(t.quantum == 0);
    ASSERT(t.runtime == 0);
    return 0;
}

/* --- Freelist / O(1) --- */
static int test_freelist_reuse(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int vals[20];
    for (int i = 0; i < 20; i++) vals[i] = i;
    for (int round = 0; round < 3; round++) {
        for (int i = 0; i < 10; i++)
            pq_push(&pq, round % 2, recorder, &vals[i]);
        pq_task_t t;
        for (int i = 0; i < 10; i++)
            ASSERT(pq_pop(&pq, &t) == 0);
        ASSERT(pq_is_empty(&pq));
    }
    return 0;
}

static int test_full_queue(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int dummy = 0;
    for (int i = 0; i < PQ_MAX_ITEMS; i++)
        ASSERT(pq_push(&pq, 0, recorder, &dummy) >= 0);
    ASSERT(pq_push(&pq, 0, recorder, &dummy) == -1);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == 0);
    ASSERT(pq_push(&pq, 0, recorder, &dummy) >= 0);
    while (!pq_is_empty(&pq)) ASSERT(pq_pop(&pq, &t) == 0);
    return 0;
}

static int test_empty_pop(void) {
    priority_queue_t pq;
    pq_init(&pq);
    pq_task_t t;
    ASSERT(pq_pop(&pq, &t) == -1);
    ASSERT(pq_pop_from_layer(&pq, 0, &t) == -1);
    return 0;
}

static int test_invalid_args(void) {
    priority_queue_t pq;
    pq_init(&pq);
    int dummy = 0;
    pq_task_t t;
    ASSERT(pq_push(NULL, 0, recorder, &dummy) == -1);
    ASSERT(pq_push(&pq, -1, recorder, &dummy) == -1);
    ASSERT(pq_push(&pq, PQ_NUM_PRIORITIES, recorder, &dummy) == -1);
    ASSERT(pq_pop(NULL, &t) == -1);
    ASSERT(pq_pop(&pq, NULL) == -1);
    ASSERT(pq_update(&pq, -1, 0) == -1);
    ASSERT(pq_remove(&pq, -1) == -1);
    return 0;
}

int main(void) {
    printf("=== PQ/MLQ Test Suite ===\n");
    printf("--- Invariants ---\n");
    RUN_TEST(fifo_tiebreak);
    RUN_TEST(priority_ordering);
    RUN_TEST(stable_repeated);
    RUN_TEST(update_priority);
    RUN_TEST(layer_scan);
    RUN_TEST(pop_from_layer);
    RUN_TEST(remove);
    RUN_TEST(stale_handle);
    printf("--- Scheduler-grade ---\n");
    RUN_TEST(set_quantum);
    RUN_TEST(quantum_runtime_defaults);
    printf("--- Freelist / O(1) ---\n");
    RUN_TEST(freelist_reuse);
    RUN_TEST(full_queue);
    printf("--- Edge cases ---\n");
    RUN_TEST(empty_pop);
    RUN_TEST(invalid_args);
    printf("=== All %d tests passed.\n", 14);
    return 0;
}
