/**
 * P1-8 workqueue acceptance (**#242**): MLQ churn, shutdown discard, layer ordering.
 */
#include "workqueue.h"

#include <stdio.h>
#include <string.h>

#define RUN_TEST(name)                          \
    do {                                        \
        printf("%s... ", #name);                \
        if (test_##name() != 0) {               \
            fprintf(stderr, "FAIL\n");          \
            return 1;                           \
        }                                       \
        printf("ok\n");                           \
    } while (0)

#define ASSERT(c)                               \
    do {                                        \
        if (!(c)) {                             \
            fprintf(stderr, "FAIL: %s\n", #c);  \
            return 1;                           \
        }                                       \
    } while (0)

static unsigned s_noop_ran;

static void noop_work(void *ctx) {
    (void)ctx;
    s_noop_ran++;
}

static int test_churn_1000(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {
        .fn = noop_work,
        .ctx = NULL,
        .tag = 0,
        .pq_layer = FL_WQ_LAYER_IDLE,
    };
    unsigned submitted = 0;

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);

    while (submitted < 1000u) {
        fl_result_t r = fl_wq_enqueue(wq, &w);

        if (r == FL_RESULT_OK) {
            submitted++;
            continue;
        }
        ASSERT(r == FL_RESULT_BUSY);
        ASSERT(fl_wq_poll(wq, FL_WQ_PENDING_MAX) > 0);
    }

    ASSERT(fl_wq_drain(wq, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_pending(wq) == 0);
    ASSERT(s_noop_ran == 1000u);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_shutdown_discards_pending(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {
        .fn = noop_work,
        .ctx = NULL,
        .tag = 0,
        .pq_layer = FL_WQ_LAYER_BACKGROUND,
    };
    unsigned i;

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);

    for (i = 0; i < 20u; i++)
        ASSERT(fl_wq_enqueue(wq, &w) == FL_RESULT_OK);
    ASSERT(fl_wq_pending(wq) == 20);

    fl_wq_shutdown(wq);
    ASSERT(fl_wq_pending(wq) == 0);
    ASSERT(s_noop_ran == 0);

    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(wq, &w) == FL_RESULT_OK);
    (void)fl_wq_poll(wq, 1);
    ASSERT(s_noop_ran == 1);
    fl_wq_shutdown(wq);
    return 0;
}

static unsigned s_layer_order[4];
static unsigned s_layer_count;

static void layer_recorder(void *ctx) {
    int layer = *(int *)ctx;

    if (layer >= 0 && layer < 4 && s_layer_count < 4)
        s_layer_order[s_layer_count++] = (unsigned)layer;
}

static int test_layer_priority(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    int ctx_urgent = FL_WQ_LAYER_URGENT;
    int ctx_back = FL_WQ_LAYER_BACKGROUND;
    fl_wq_work_t w_urgent = {
        .fn = layer_recorder,
        .ctx = &ctx_urgent,
        .tag = 0,
        .pq_layer = FL_WQ_LAYER_URGENT,
    };
    fl_wq_work_t w_back = {
        .fn = layer_recorder,
        .ctx = &ctx_back,
        .tag = 0,
        .pq_layer = FL_WQ_LAYER_BACKGROUND,
    };

    s_layer_count = 0;
    memset(s_layer_order, 0, sizeof(s_layer_order));
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);

    ASSERT(fl_wq_enqueue(wq, &w_back) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(wq, &w_urgent) == FL_RESULT_OK);
    (void)fl_wq_poll(wq, 2);

    ASSERT(s_layer_count == 2);
    ASSERT(s_layer_order[0] == (unsigned)FL_WQ_LAYER_URGENT);
    ASSERT(s_layer_order[1] == (unsigned)FL_WQ_LAYER_BACKGROUND);
    fl_wq_shutdown(wq);
    return 0;
}

int main(void) {
    RUN_TEST(churn_1000);
    RUN_TEST(shutdown_discards_pending);
    RUN_TEST(layer_priority);
    printf("test_workqueue_p18: all passed\n");
    return 0;
}
