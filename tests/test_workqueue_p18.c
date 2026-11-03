/**
 * P1-8 workqueue acceptance (**#242**): MLQ churn, shutdown discard, layer ordering.
 */
#include "workqueue.h"

#include "bg_jobs.h"
#include "net_background.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
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

/* bg_jobs.c is linked here with the network background stubbed out: the test needs to see where the
 * lazy init runs, and the tick must do nothing else. */
static pthread_t s_main_thread;
static int s_net_init_calls;
static int s_net_init_off_main;
static _Atomic int s_net_ticks;

void fl_net_background_init(void) {
    s_net_init_calls++;
    if (!pthread_equal(pthread_self(), s_main_thread))
        s_net_init_off_main = 1;
}
void fl_net_background_shutdown(void) {}
void fl_net_background_tick(unsigned max_items) {
    (void)max_items;
    atomic_fetch_add(&s_net_ticks, 1);
}
fl_result_t fl_net_background_arp_tick_kick(void) { return FL_RESULT_OK; }

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
    ASSERT(fl_wq_poll(wq, 1) == 1);
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
    ASSERT(fl_wq_poll(wq, 2) == 2);

    ASSERT(s_layer_count == 2);
    ASSERT(s_layer_order[0] == (unsigned)FL_WQ_LAYER_URGENT);
    ASSERT(s_layer_order[1] == (unsigned)FL_WQ_LAYER_BACKGROUND);
    fl_wq_shutdown(wq);
    return 0;
}

/* ---- FL-22: delayed, periodic and protected work ---- */

static int64_t s_order[8];
static unsigned s_order_n;

static void order_recorder(void *ctx) {
    if (s_order_n < 8)
        s_order[s_order_n++] = *(int64_t *)ctx;
}

static int test_delayed_runs_when_due(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &w, 1000) == FL_RESULT_OK);
    ASSERT(fl_wq_pending(wq) == 1);
    ASSERT(fl_wq_poll_at(wq, 8, 999) == 0);
    ASSERT(s_noop_ran == 0);
    ASSERT(fl_wq_pending(wq) == 1);
    ASSERT(fl_wq_poll_at(wq, 8, 1000) == 1);
    ASSERT(s_noop_ran == 1);
    ASSERT(fl_wq_pending(wq) == 0);
    ASSERT(fl_wq_poll_at(wq, 8, 5000) == 0);
    ASSERT(fl_wq_enqueue_at(wq, &w, 0) == FL_RESULT_INVAL);
    ASSERT(fl_wq_enqueue_at(wq, &w, -5) == FL_RESULT_INVAL);
    ASSERT(fl_wq_enqueue_every(wq, &w, 0) == FL_RESULT_INVAL);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_timers_promote_in_due_order(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    int64_t t[3] = {300, 100, 200};
    fl_wq_work_t w[3];

    s_order_n = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    for (int i = 0; i < 3; i++) {
        memset(&w[i], 0, sizeof w[i]);
        w[i].fn = order_recorder;
        w[i].ctx = &t[i];
        w[i].pq_layer = FL_WQ_LAYER_NORMAL;
        ASSERT(fl_wq_enqueue_at(wq, &w[i], t[i]) == FL_RESULT_OK);
    }
    ASSERT(fl_wq_poll_at(wq, 8, 250) == 2);
    ASSERT(s_order_n == 2 && s_order[0] == 100 && s_order[1] == 200);
    ASSERT(fl_wq_poll_at(wq, 8, 400) == 1);
    ASSERT(s_order_n == 3 && s_order[2] == 300);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_due_timer_keeps_layer_priority(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    int64_t back = 1, urgent = 2;
    fl_wq_work_t wb = {.fn = order_recorder, .ctx = &back, .pq_layer = FL_WQ_LAYER_BACKGROUND};
    fl_wq_work_t wu = {.fn = order_recorder, .ctx = &urgent, .pq_layer = FL_WQ_LAYER_URGENT};

    s_order_n = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &wb, 10) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &wu, 20) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 30) == 2);
    ASSERT(s_order[0] == 2 && s_order[1] == 1);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_periodic_rearms(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL, .period_ns = 100};

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &w, 500) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 500) == 1);
    ASSERT(s_noop_ran == 1);
    ASSERT(fl_wq_pending(wq) == 1); /* waiting for 600 */
    ASSERT(fl_wq_poll_at(wq, 8, 599) == 0);
    ASSERT(fl_wq_poll_at(wq, 8, 600) == 1);
    ASSERT(s_noop_ran == 2);
    /* a late poll runs it once and re-arms one period after the poll, not a burst of catch-up runs */
    ASSERT(fl_wq_poll_at(wq, 8, 10000) == 1);
    ASSERT(fl_wq_poll_at(wq, 8, 10050) == 0);
    ASSERT(fl_wq_poll_at(wq, 8, 10100) == 1);
    ASSERT(s_noop_ran == 4);
    fl_wq_shutdown(wq); /* frees the re-armed item: leak check under ASan */
    ASSERT(fl_wq_pending(wq) == 0);
    return 0;
}

static int test_enqueue_every_uses_the_clock(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_every(wq, &w, 3600LL * 1000000000LL) == FL_RESULT_OK);
    ASSERT(fl_wq_poll(wq, 8) == 0); /* an hour away */
    ASSERT(s_noop_ran == 0);
    ASSERT(fl_wq_pending(wq) == 1);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_timer_slots_are_bounded(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL, .period_ns = 10};

    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
        ASSERT(fl_wq_enqueue_at(wq, &w, 100 + (int64_t)i) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &w, 5000) == FL_RESULT_BUSY);
    /* promoting periodic items keeps their slot: re-arming can never fail */
    ASSERT(fl_wq_poll_at(wq, 1, 1000) == 1);
    ASSERT(fl_wq_enqueue_at(wq, &w, 5000) == FL_RESULT_BUSY);
    ASSERT(fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, 1000) > 0);
    ASSERT(fl_wq_pending(wq) == (int)FL_WQ_TIMERS_MAX);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_protected_reserve(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t ord = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_IDLE};
    fl_wq_work_t prot = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_URGENT, .flags = FL_WQ_PROTECTED};
    unsigned n = 0;

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    while (fl_wq_enqueue(wq, &ord) == FL_RESULT_OK)
        n++;
    ASSERT(n == FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE);
    for (unsigned i = 0; i < FL_WQ_PROTECTED_RESERVE; i++)
        ASSERT(fl_wq_enqueue(wq, &prot) == FL_RESULT_OK);
    ASSERT(fl_wq_pending(wq) == (int)FL_WQ_PENDING_MAX);
    ASSERT(fl_wq_enqueue(wq, &prot) == FL_RESULT_BUSY); /* never dropped: the caller must handle BUSY */
    ASSERT(fl_wq_poll(wq, FL_WQ_PENDING_MAX) == FL_WQ_PENDING_MAX);
    ASSERT(s_noop_ran == FL_WQ_PENDING_MAX);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_protected_survives_shutdown(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    int64_t tag_p = 7, tag_o = 9, tag_t = 11;
    fl_wq_work_t ord = {.fn = order_recorder, .ctx = &tag_o, .pq_layer = FL_WQ_LAYER_NORMAL};
    fl_wq_work_t prot = {.fn = order_recorder, .ctx = &tag_p, .pq_layer = FL_WQ_LAYER_NORMAL,
                         .flags = FL_WQ_PROTECTED};
    fl_wq_work_t timed = {.fn = order_recorder, .ctx = &tag_t, .pq_layer = FL_WQ_LAYER_NORMAL,
                          .flags = FL_WQ_PROTECTED, .period_ns = 50};

    s_order_n = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(wq, &ord) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(wq, &prot) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_at(wq, &timed, 1000000) == FL_RESULT_OK);
    fl_wq_shutdown(wq);
    ASSERT(fl_wq_pending(wq) == 0);
    ASSERT(s_order_n == 2); /* protected ones ran once; the ordinary one was discarded */
    ASSERT(s_order[0] == 7 && s_order[1] == 11);
    ASSERT(fl_wq_enqueue(wq, &prot) == FL_RESULT_BUSY); /* nothing is accepted after shutdown */
    return 0;
}

static fl_workqueue_t *s_inner_wq;
static unsigned s_inner_polled;

static void poll_other_queue(void *ctx) {
    (void)ctx;
    s_inner_polled = fl_wq_poll_at(s_inner_wq, 4, 0);
}

static int test_poll_guard_is_per_queue(void) {
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa;
    fl_workqueue_t *b = (fl_workqueue_t *)&sb;
    fl_wq_work_t outer = {.fn = poll_other_queue, .pq_layer = FL_WQ_LAYER_NORMAL};
    fl_wq_work_t inner = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};

    s_noop_ran = 0;
    s_inner_polled = 0;
    ASSERT(fl_wq_init(a) == FL_RESULT_OK);
    ASSERT(fl_wq_init(b) == FL_RESULT_OK);
    s_inner_wq = b;
    ASSERT(fl_wq_enqueue(b, &inner) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(a, &outer) == FL_RESULT_OK);
    /* a debug build used to abort here: one file-static depth counter covered every queue */
    ASSERT(fl_wq_poll_at(a, 4, 0) == 1);
    ASSERT(s_inner_polled == 1 && s_noop_ran == 1);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    return 0;
}

#define MT_THREADS 4
#define MT_EACH 6

static void *mt_enqueuer(void *arg) {
    fl_workqueue_t *wq = (fl_workqueue_t *)arg;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};
    fl_wq_work_t d = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};

    for (int i = 0; i < MT_EACH; i++) {
        if (fl_wq_enqueue(wq, &w) != FL_RESULT_OK)
            return (void *)1;
        if (fl_wq_enqueue_at(wq, &d, 10 + i) != FL_RESULT_OK)
            return (void *)1;
    }
    return NULL;
}

static int test_concurrent_enqueue(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    pthread_t th[MT_THREADS];
    void *rv;

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    /* 48 items in all: under the ordinary cap (56) and 24 timers under FL_WQ_TIMERS_MAX */
    for (int i = 0; i < MT_THREADS; i++)
        ASSERT(pthread_create(&th[i], NULL, mt_enqueuer, wq) == 0);
    for (int i = 0; i < MT_THREADS; i++) {
        ASSERT(pthread_join(th[i], &rv) == 0);
        ASSERT(rv == NULL);
    }
    ASSERT(fl_wq_pending(wq) == 2 * MT_THREADS * MT_EACH);
    ASSERT(fl_wq_poll_at(wq, 200, 1000000) == 2u * MT_THREADS * MT_EACH);
    ASSERT(s_noop_ran == 2u * MT_THREADS * MT_EACH);
    fl_wq_shutdown(wq);
    return 0;
}

/* ---- review fixes: overflow, ordering, slot accounting, ticker ---- */

static int test_period_overflow_is_refused_and_saturates(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t huge = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL, .period_ns = INT64_MAX};
    fl_wq_work_t big = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL, .period_ns = INT64_MAX - 20};

    s_noop_ran = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    /* first run + one period would not fit */
    ASSERT(fl_wq_enqueue_at(wq, &huge, 10) == FL_RESULT_INVAL);
    /* now + period would not fit */
    ASSERT(fl_wq_enqueue_every(wq, &huge, INT64_MAX) == FL_RESULT_INVAL);
    ASSERT(fl_wq_pending(wq) == 0);
    /* accepted (10 + MAX-20 fits) but the re-arm after a poll at 100 would wrap: it saturates, so the
     * item is not due again, instead of wrapping negative and running on every poll */
    ASSERT(fl_wq_enqueue_at(wq, &big, 10) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 100) == 1);
    ASSERT(fl_wq_poll_at(wq, 8, 200) == 0);
    ASSERT(fl_wq_poll_at(wq, 8, 300) == 0);
    ASSERT(s_noop_ran == 1);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_equal_due_times_keep_arrival_order(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    int64_t id[4] = {1, 2, 3, 4};
    fl_wq_work_t w[4];

    s_order_n = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    for (int i = 0; i < 4; i++) {
        memset(&w[i], 0, sizeof w[i]);
        w[i].fn = order_recorder;
        w[i].ctx = &id[i];
        w[i].pq_layer = FL_WQ_LAYER_NORMAL;
        ASSERT(fl_wq_enqueue_at(wq, &w[i], 500) == FL_RESULT_OK);
    }
    ASSERT(fl_wq_poll_at(wq, 8, 500) == 4);
    ASSERT(s_order[0] == 1 && s_order[1] == 2 && s_order[2] == 3 && s_order[3] == 4);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_periodic_slots_are_returned(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t per = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL, .period_ns = 100};
    fl_wq_work_t once = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_NORMAL};
    int64_t now = 1000;

    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    /* one periodic item and the rest one-shot timers: every round the one-shots finish and free their
     * slots, so exactly FL_WQ_TIMERS_MAX - 1 more must fit. A periodic slot that leaked on re-arm
     * (periodic_inflight never decremented) would make a later round come up short. */
    ASSERT(fl_wq_enqueue_at(wq, &per, now) == FL_RESULT_OK);
    for (int round = 0; round < 5; round++) {
        for (unsigned i = 0; i + 1 < FL_WQ_TIMERS_MAX; i++)
            ASSERT(fl_wq_enqueue_at(wq, &once, now) == FL_RESULT_OK);
        ASSERT(fl_wq_enqueue_at(wq, &once, now) == FL_RESULT_BUSY);
        ASSERT(fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, now) == FL_WQ_TIMERS_MAX);
        ASSERT(fl_wq_pending(wq) == 1); /* only the re-armed periodic item is left */
        now += 100;
    }
    /* a full set of periodic items, run many times, stays full and never over-full */
    fl_wq_shutdown(wq);
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
        ASSERT(fl_wq_enqueue_at(wq, &per, 100) == FL_RESULT_OK);
    for (int round = 0; round < 6; round++) {
        ASSERT(fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, 100 + 100 * round) == FL_WQ_TIMERS_MAX);
        ASSERT(fl_wq_pending(wq) == (int)FL_WQ_TIMERS_MAX);
        ASSERT(fl_wq_enqueue_at(wq, &per, 99999) == FL_RESULT_BUSY);
    }
    fl_wq_shutdown(wq);
    return 0;
}

static _Atomic unsigned s_mt_ran;

static void mt_count(void *ctx) {
    (void)ctx;
    atomic_fetch_add(&s_mt_ran, 1);
}

static fl_workqueue_t *s_default_seen[4];

static void *default_getter(void *arg) {
    s_default_seen[(intptr_t)arg] = fl_wq_default();
    return NULL;
}

static void *default_enqueuer(void *arg) {
    fl_wq_work_t w = {.fn = mt_count, .pq_layer = FL_WQ_LAYER_NORMAL};
    (void)arg;
    for (int i = 0; i < 10; i++) {
        fl_result_t r;
        while ((r = fl_wq_enqueue(fl_wq_default(), &w)) == FL_RESULT_BUSY) {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
        if (r != FL_RESULT_OK)
            return (void *)1;
    }
    return NULL;
}

static unsigned long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)ts.tv_nsec / 1000000ull;
}

static int test_ticker_and_default_queue(void) {
    pthread_t th[4];
    void *rv;
    unsigned long long t0;

    s_main_thread = pthread_self();
    /* pthread_once: every thread sees the one default queue */
    for (intptr_t i = 0; i < 4; i++)
        ASSERT(pthread_create(&th[i], NULL, default_getter, (void *)i) == 0);
    for (int i = 0; i < 4; i++)
        ASSERT(pthread_join(th[i], NULL) == 0);
    for (int i = 1; i < 4; i++)
        ASSERT(s_default_seen[i] == s_default_seen[0] && s_default_seen[0] != NULL);

    ASSERT(fl_bg_jobs_ticker_start(0) == FL_RESULT_INVAL);
    ASSERT(s_net_init_calls == 0);
    ASSERT(fl_bg_jobs_ticker_start(10000) == FL_RESULT_OK); /* long period: stop must not wait for it */
    ASSERT(s_net_init_calls == 1 && !s_net_init_off_main); /* net init ran on the starting thread, first */
    ASSERT(fl_bg_jobs_ticker_start(10) == FL_RESULT_BUSY);
    t0 = mono_ms();
    while (atomic_load(&s_net_ticks) < 1 && mono_ms() - t0 < 5000) {
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
    }
    t0 = mono_ms();
    fl_bg_jobs_ticker_stop();
    ASSERT(mono_ms() - t0 < 2000);
    fl_bg_jobs_ticker_stop(); /* second stop is a no-op */
    ASSERT(atomic_load(&s_net_ticks) >= 1); /* it ticked at least once before it stopped */

    /* restart with a short period; three threads enqueue while it polls */
    atomic_store(&s_mt_ran, 0);
    ASSERT(fl_bg_jobs_ticker_start(1) == FL_RESULT_OK);
    for (int i = 0; i < 3; i++)
        ASSERT(pthread_create(&th[i], NULL, default_enqueuer, NULL) == 0);
    for (int i = 0; i < 3; i++) {
        ASSERT(pthread_join(th[i], &rv) == 0);
        ASSERT(rv == NULL);
    }
    t0 = mono_ms();
    while (atomic_load(&s_mt_ran) < 30 && mono_ms() - t0 < 10000) {
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, NULL);
    }
    ASSERT(atomic_load(&s_mt_ran) == 30);
    fl_bg_jobs_ticker_stop();
    ASSERT(s_net_init_off_main == 0);

    /* shutdown of the default queue runs protected items once and drops the rest */
    {
        fl_wq_work_t prot = {.fn = mt_count, .pq_layer = FL_WQ_LAYER_NORMAL, .flags = FL_WQ_PROTECTED};
        fl_wq_work_t ord = {.fn = mt_count, .pq_layer = FL_WQ_LAYER_NORMAL};

        atomic_store(&s_mt_ran, 0);
        ASSERT(fl_wq_enqueue(fl_wq_default(), &prot) == FL_RESULT_OK);
        ASSERT(fl_wq_enqueue(fl_wq_default(), &ord) == FL_RESULT_OK);
        fl_bg_jobs_shutdown();
        ASSERT(atomic_load(&s_mt_ran) == 1);
    }
    return 0;
}

int main(void) {
    RUN_TEST(churn_1000);
    RUN_TEST(shutdown_discards_pending);
    RUN_TEST(layer_priority);
    RUN_TEST(delayed_runs_when_due);
    RUN_TEST(timers_promote_in_due_order);
    RUN_TEST(due_timer_keeps_layer_priority);
    RUN_TEST(periodic_rearms);
    RUN_TEST(enqueue_every_uses_the_clock);
    RUN_TEST(timer_slots_are_bounded);
    RUN_TEST(protected_reserve);
    RUN_TEST(protected_survives_shutdown);
    RUN_TEST(poll_guard_is_per_queue);
    RUN_TEST(concurrent_enqueue);
    RUN_TEST(period_overflow_is_refused_and_saturates);
    RUN_TEST(equal_due_times_keep_arrival_order);
    RUN_TEST(periodic_slots_are_returned);
    RUN_TEST(ticker_and_default_queue); /* last: it shuts the default queue down */
    printf("test_workqueue_p18: all passed\n");
    return 0;
}
