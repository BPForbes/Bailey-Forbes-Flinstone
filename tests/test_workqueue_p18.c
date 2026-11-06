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
#include <stdlib.h>
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

/* The same storage serves several lives: shutdown, destroy the mutex, initialise again. Initialising a live
 * queue again would re-initialise an initialised mutex, which pthreads does not allow. */
static int test_storage_is_reusable_after_destroy(void) {
    fl_workqueue_storage_t store;
    fl_workqueue_t *wq = (fl_workqueue_t *)&store;
    fl_wq_work_t w = {.fn = noop_work, .pq_layer = FL_WQ_LAYER_BACKGROUND};

    s_noop_ran = 0;
    for (unsigned life = 1; life <= 3; life++) {
        ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
        ASSERT(fl_wq_enqueue(wq, &w) == FL_RESULT_OK);
        ASSERT(fl_wq_drain(wq, 0) == FL_RESULT_OK);
        ASSERT(s_noop_ran == life);
        fl_wq_shutdown(wq);
        ASSERT(fl_wq_enqueue(wq, &w) == FL_RESULT_BUSY); /* a shut down queue refuses work */
        fl_wq_destroy(wq);
    }
    return 0;
}

/* The generated state machine (formal/lean) is exactly the twelve moves the queue makes, and only those: every
 * pair of numbers 0..255 is checked against a list written here, not derived from the generated code. */
static int test_model_table_is_pinned(void) {
    static const unsigned moves[][2] = {
        {FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED}, {FL_WQ_ST_PENDING, FL_WQ_ST_HELD},
        {FL_WQ_ST_PENDING, FL_WQ_ST_EXPIRED}, {FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING},
        {FL_WQ_ST_QUEUED, FL_WQ_ST_PENDING}, {FL_WQ_ST_QUEUED, FL_WQ_ST_EXPIRED},
        {FL_WQ_ST_QUEUED, FL_WQ_ST_FAILED}, {FL_WQ_ST_RUNNING, FL_WQ_ST_DONE},
        {FL_WQ_ST_RUNNING, FL_WQ_ST_PENDING}, {FL_WQ_ST_RUNNING, FL_WQ_ST_FAILED},
        {FL_WQ_ST_HELD, FL_WQ_ST_PENDING}, {FL_WQ_ST_HELD, FL_WQ_ST_SKIPPED},
    };

    for (unsigned f = 0; f < 256u; f++) {
        for (unsigned to = 0; to < 256u; to++) {
            unsigned want = 0;

            for (size_t k = 0; k < sizeof moves / sizeof moves[0]; k++)
                want |= moves[k][0] == f && moves[k][1] == to;
            ASSERT(fl_wq_model_transition_allowed(f, to) == want);
        }
        ASSERT(fl_wq_model_state_terminal(f) ==
               (f == FL_WQ_ST_DONE || f == FL_WQ_ST_FAILED || f == FL_WQ_ST_EXPIRED || f == FL_WQ_ST_SKIPPED));
    }
    ASSERT(fl_wq_model_transition_allowed(256u, 1u) == 0 && fl_wq_model_transition_allowed(1u, 4096u) == 0);
    return 0;
}

/* FL-22 design values published in contract_p1_workqueue.h (awaiting owner confirm). Hosted builds
 * carry a per-queue mutex; freestanding is covered by test_workqueue_freestanding. */
static int test_hosted_capacity_and_lock_are_pinned(void) {
    fl_workqueue_storage_t st;

    ASSERT(FL_WQ_PENDING_MAX == 64u);
    ASSERT(FL_WQ_PROTECTED_RESERVE == 8u);
    ASSERT(FL_WQ_TIMERS_MAX == 32u);
    ASSERT(FL_WQ_PROTECTED_RESERVE < FL_WQ_PENDING_MAX);
#ifdef FL_WQ_HAS_LOCK
    ASSERT(sizeof st.lock >= sizeof(pthread_mutex_t));
#else
#error "hosted test_workqueue_p18 must see FL_WQ_HAS_LOCK"
#endif
    (void)st;
    return 0;
}

/* Runs last: every durable scenario above (accept, run, retry, fail, recover, expire, hold, resolve) moved jobs
 * only along the model's transitions; a move the model forbids is refused and counted. */
static int test_the_queue_never_left_the_model(void) {
    ASSERT(fl_wq_illegal_moves() == 0);
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

static int ticker_default_queue_is_shared(void) {
    pthread_t th[4];

    /* pthread_once: every thread sees the one default queue */
    for (intptr_t i = 0; i < 4; i++)
        ASSERT(pthread_create(&th[i], NULL, default_getter, (void *)i) == 0);
    for (int i = 0; i < 4; i++)
        ASSERT(pthread_join(th[i], NULL) == 0);
    for (int i = 1; i < 4; i++)
        ASSERT(s_default_seen[i] == s_default_seen[0] && s_default_seen[0] != NULL);
    return 0;
}

static int ticker_starts_and_stops_promptly(void) {
    unsigned long long t0;

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
    return 0;
}

static int ticker_runs_concurrent_enqueues(void) {
    pthread_t th[4];
    void *rv;
    unsigned long long t0;

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
    return 0;
}

/* Shutdown of the default queue runs protected items once and drops the rest. */
static int default_shutdown_runs_protected_once(void) {
    fl_wq_work_t prot = {.fn = mt_count, .pq_layer = FL_WQ_LAYER_NORMAL, .flags = FL_WQ_PROTECTED};
    fl_wq_work_t ord = {.fn = mt_count, .pq_layer = FL_WQ_LAYER_NORMAL};

    atomic_store(&s_mt_ran, 0);
    ASSERT(fl_wq_enqueue(fl_wq_default(), &prot) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue(fl_wq_default(), &ord) == FL_RESULT_OK);
    fl_bg_jobs_shutdown();
    ASSERT(atomic_load(&s_mt_ran) == 1);
    return 0;
}

static int test_ticker_and_default_queue(void) {
    s_main_thread = pthread_self();
    ASSERT(ticker_default_queue_is_shared() == 0);
    ASSERT(ticker_starts_and_stops_promptly() == 0);
    ASSERT(ticker_runs_concurrent_enqueues() == 0);
    ASSERT(default_shutdown_runs_protected_once() == 0);
    return 0;
}

/* ---- FL-25: durable work on the memory journal ---- */

/* A queue that runs durable work names its executor; fl_wq_init alone runs none. */
static fl_result_t dq_init(fl_workqueue_t *wq) {
    fl_result_t rc = fl_wq_init(wq);

    return rc != FL_RESULT_OK ? rc : fl_wq_set_executor(wq, FL_WQ_EXECUTOR_DEFAULT);
}

static void mkkey(uint8_t k[16], unsigned n) {
    memset(k, 0, 16);
    k[0] = (uint8_t)n;
    k[1] = (uint8_t)(n >> 8);
    k[2] = 0xA5;
}

static unsigned s_dur_ran, s_dur_fail_left;
static uint8_t s_dur_last_payload;

static fl_result_t dur_handler(const void *payload, size_t n, const uint8_t idem[16]) {
    (void)idem;
    s_dur_ran++;
    if (n)
        s_dur_last_payload = *(const uint8_t *)payload;
    if (s_dur_fail_left) {
        s_dur_fail_left--;
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

#define TAG_AUTO 0x7001u
#define TAG_ASK 0x7002u

static int register_dur_tags(void) {
    static int done;

    if (done)
        return 0;
    ASSERT(fl_wq_register_handler(TAG_AUTO, dur_handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_OK);
    ASSERT(fl_wq_register_handler(TAG_ASK, dur_handler, FL_WQ_RECOVER_ASK) == FL_RESULT_OK);
    ASSERT(fl_wq_register_handler(TAG_AUTO, dur_handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_BUSY);
    ASSERT(fl_wq_register_handler(0x7FFF, NULL, FL_WQ_RECOVER_AUTO) == FL_RESULT_INVAL);
    done = 1;
    return 0;
}

static int test_durable_runs_once_and_is_idempotent(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t key[16], pl = 42;

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    s_dur_fail_left = 0;
    mkkey(key, 1);
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(wq, 0x7EEE, FL_WQ_LAYER_NORMAL, NULL, &pl, 1, key, 0) == FL_RESULT_NOENT); /* no handler */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, &pl, FL_WQ_PAYLOAD_MAX + 1, key, 0) ==
           FL_RESULT_INVAL);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, &pl, 1, key, 0) == FL_RESULT_OK);
    ASSERT(j->count(j->ctx, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 1); /* accepted, persisted, staged */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1);
    ASSERT(s_dur_ran == 1 && s_dur_last_payload == 42);
    ASSERT(j->count(j->ctx, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    /* the same key again: accepted as the same job, nothing new runs */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, &pl, 1, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0);
    ASSERT(s_dur_ran == 1);
    fl_wq_shutdown(wq);
    return 0;
}

/* An owner label with no terminator inside the allowed length must be refused without reading past the
 * buffer (ASan fails this if the length scan indexes before it checks the bound). */
static int test_durable_owner_bound(void) {
    uint8_t key[16];
    char *exact = (char *)malloc(FL_HERITAGE_KEY_ID_MAX); /* exactly the limit, no NUL at all */
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;

    ASSERT(exact != NULL);
    memset(exact, 'a', FL_HERITAGE_KEY_ID_MAX);
    mkkey(key, 31);
    fl_wq_journal_mem_reset();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, exact, NULL, 0, key, 0) == FL_RESULT_INVAL);
    exact[FL_HERITAGE_KEY_ID_MAX - 1] = '\0'; /* the longest label that fits */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, exact, NULL, 0, key, 0) == FL_RESULT_OK);
    fl_wq_shutdown(wq);
    free(exact);
    return 0;
}

/* A ttl that would push the deadline past INT64_MAX is refused, not wrapped into the past. */
static int test_durable_ttl_overflow(void) {
    uint8_t key[16];
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;

    mkkey(key, 32);
    fl_wq_journal_mem_reset();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, INT64_MAX) == FL_RESULT_INVAL);
    ASSERT(fl_wq_pending(wq) == 0); /* nothing was queued */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 3600) == FL_RESULT_OK);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_durable_retries_then_fails(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t key[16];

    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    s_dur_fail_left = 100;
    mkkey(key, 2);
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    for (int i = 0; i < 10; i++)
        (void)fl_wq_poll_at(wq, 8, 0);
    ASSERT(s_dur_ran == FL_WQ_MAX_ATTEMPTS);
    ASSERT(j->count(j->ctx, FL_WQ_ST_FAILED, NULL, FL_WQ_EXECUTOR_ANY) == 1 && j->count(j->ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 0);
    s_dur_fail_left = 0;
    fl_wq_shutdown(wq);
    return 0;
}

/* One cut off mid-run with attempts left, one with none left. */
static int seed_cut_off_jobs(const fl_wq_journal_ops_t *j, const uint8_t k2[16], const uint8_t k3[16]) {
    fl_wq_journal_record_t r;
    uint64_t id;
    int ex;

    memset(&r, 0, sizeof r);
    r.executor = FL_WQ_EXECUTOR_DEFAULT; r.tag = TAG_AUTO; r.layer = FL_WQ_LAYER_NORMAL; r.max_attempts = FL_WQ_MAX_ATTEMPTS;
    memcpy(r.idem_key, k2, 16);
    ASSERT(j->append(j->ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK && !ex);
    ASSERT(j->mark(j->ctx, id, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_OK);
    ASSERT(j->mark(j->ctx, id, FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING) == FL_RESULT_OK); /* attempts 1 */
    memcpy(r.idem_key, k3, 16);
    ASSERT(j->append(j->ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK && !ex);
    ASSERT(j->mark(j->ctx, id, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_OK);
    for (unsigned i = 0; i < FL_WQ_MAX_ATTEMPTS; i++) { /* run it to its attempt limit, still RUNNING */
        ASSERT(j->mark(j->ctx, id, FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING) == FL_RESULT_OK);
        if (i + 1 < FL_WQ_MAX_ATTEMPTS)
            ASSERT(j->mark(j->ctx, id, FL_WQ_ST_RUNNING, FL_WQ_ST_QUEUED) == FL_RESULT_OK);
    }
    return 0;
}

static int test_durable_recovers_after_a_restart(void) {
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t k1[16], k2[16], k3[16];

    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    s_dur_fail_left = 0;
    mkkey(k1, 11);
    mkkey(k2, 12);
    mkkey(k3, 13);
    ASSERT(dq_init(a) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, k1, 0) == FL_RESULT_OK);
    fl_wq_shutdown(a); /* the process dies: the memory window is gone, the journal is not */
    ASSERT(s_dur_ran == 0 && j->count(j->ctx, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    ASSERT(seed_cut_off_jobs(j, k2, k3) == 0);

    ASSERT(dq_init(b) == FL_RESULT_OK);
    ASSERT(fl_wq_recover(b) == FL_RESULT_OK);
    ASSERT(j->count(j->ctx, FL_WQ_ST_FAILED, NULL, FL_WQ_EXECUTOR_ANY) == 1); /* the one with no attempts left */
    ASSERT(fl_wq_poll_at(b, 8, 0) == 2);                   /* the staged one and the cut-off one run */
    ASSERT(s_dur_ran == 2);
    ASSERT(j->count(j->ctx, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == 2);
    fl_wq_shutdown(b);
    return 0;
}

static int test_stale_durable_work_expires_unrun(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    fl_wq_journal_record_t r;
    uint64_t id;
    int ex;

    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    memset(&r, 0, sizeof r);
    r.executor = FL_WQ_EXECUTOR_DEFAULT; r.tag = TAG_AUTO; r.layer = FL_WQ_LAYER_NORMAL; r.max_attempts = FL_WQ_MAX_ATTEMPTS;
    r.idem_key[0] = 77;
    r.deadline = 1; /* long past */
    ASSERT(j->append(j->ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK);
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0 && s_dur_ran == 0);
    ASSERT(j->count(j->ctx, FL_WQ_ST_EXPIRED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_held_work_waits_for_the_owner(void) {
    fl_workqueue_storage_t sa;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa;
    fl_workqueue_t *dflt = fl_wq_default();
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t k1[16], k2[16], k3[16];

    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    mkkey(k1, 21);
    mkkey(k2, 22);
    mkkey(k3, 23);
    ASSERT(dq_init(a) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_ASK, FL_WQ_LAYER_BACKGROUND, "Root.Bob", NULL, 0, k1, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_ASK, FL_WQ_LAYER_BACKGROUND, "Root.Bob", NULL, 0, k2, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_ASK, FL_WQ_LAYER_BACKGROUND, "Root.Eve", NULL, 0, k3, 0) == FL_RESULT_OK);
    fl_wq_shutdown(a); /* restart */
    ASSERT(fl_wq_set_executor(dflt, FL_WQ_EXECUTOR_DEFAULT) == FL_RESULT_OK); /* the restarted default queue */
    ASSERT(fl_wq_recover(dflt) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(dflt, 8, 0) == 0 && s_dur_ran == 0); /* nothing runs silently */
    ASSERT(fl_wq_held_count("Root.Bob") == 2 && fl_wq_held_count("Root.Eve") == 1);
    /* declining drops only these occurrences */
    ASSERT(fl_wq_resolve_held("Root.Eve", 0) == FL_RESULT_OK);
    ASSERT(fl_wq_held_count("Root.Eve") == 0 && j->count(j->ctx, FL_WQ_ST_SKIPPED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    ASSERT(fl_wq_poll_at(dflt, 8, 0) == 0 && s_dur_ran == 0);
    /* accepting queues them now */
    ASSERT(fl_wq_resolve_held("Root.Bob", 1) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(dflt, 8, 0) == 2 && s_dur_ran == 2);
    ASSERT(fl_wq_held_count("Root.Bob") == 0);
    return 0;
}

static int test_durable_backlog_is_bounded_and_drains(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t key[16];
    unsigned n = 0, guard = 0;

    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    for (;; n++) {
        mkkey(key, 1000 + n);
        key[3] = (uint8_t)(n >> 8);
        fl_result_t rc = fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_IDLE, NULL, NULL, 0, key, 0);

        if (rc == FL_RESULT_BUSY)
            break;
        ASSERT(rc == FL_RESULT_OK);
        ASSERT(n < 2 * FL_WQ_JOURNAL_MAX_PENDING);
    }
    ASSERT(n >= 1000 && n <= FL_WQ_JOURNAL_MAX_PENDING); /* a clear "retry later", not a crash or a silent drop */
    ASSERT(fl_wq_pending(wq) <= (int)(FL_WQ_PENDING_MAX / 2u)); /* the memory queue holds only the window */
    while ((j->count(j->ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) || j->count(j->ctx, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY)) && guard++ < 1000)
        (void)fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, 0);
    ASSERT(s_dur_ran == n);
    ASSERT(j->count(j->ctx, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == n);
    /* finished records are reusable, so the journal accepts work again */
    mkkey(key, 5000);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_IDLE, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    fl_wq_shutdown(wq);
    return 0;
}

/* ---- FL-25: reservation, executors and journal failures, against a journal that fails on demand ---- */

/* Wraps the memory journal. Each knob is a count of calls to fail, or a hook that runs inside one. */
typedef struct {
    unsigned fail_append, fail_scan, fail_mark, fail_load;
    fl_wq_state_t fail_to;       /* a mark into this state is the one that fails */
    fl_result_t mark_result;     /* what a failing mark answers: ERR (an I/O failure) or BUSY (a lost race) */
    void (*after_scan)(void);    /* after a scan returned rows: the caller holds its reservation and the rows */
    void (*on_claim)(void);      /* after a mark into QUEUED succeeded */
} fj_t;

static fj_t s_fj;
static fl_wq_journal_ops_t s_fj_ops;

static fl_result_t fj_append(void *c, const fl_wq_journal_record_t *r, unsigned max, uint64_t *id, int *ex) {
    if (s_fj.fail_append) {
        s_fj.fail_append--;
        return FL_RESULT_ERR;
    }
    return fl_wq_journal_mem()->append(c, r, max, id, ex);
}

static fl_result_t fj_mark(void *c, uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    fl_result_t rc;

    if (s_fj.fail_mark && to == s_fj.fail_to) {
        s_fj.fail_mark--;
        return s_fj.mark_result;
    }
    rc = fl_wq_journal_mem()->mark(c, id, from, to);
    if (rc == FL_RESULT_OK && to == FL_WQ_ST_QUEUED && s_fj.on_claim)
        s_fj.on_claim();
    return rc;
}

static fl_result_t fj_load(void *c, uint64_t id, fl_wq_journal_record_t *out) {
    if (s_fj.fail_load) {
        s_fj.fail_load--;
        return FL_RESULT_ERR;
    }
    return fl_wq_journal_mem()->load(c, id, out);
}

static fl_result_t fj_scan(void *c, fl_wq_state_t st, const char *o, uint32_t ex, const fl_wq_journal_record_t *after,
                           unsigned max, fl_wq_journal_record_t *out, unsigned *n) {
    fl_result_t rc;

    if (s_fj.fail_scan) {
        s_fj.fail_scan--;
        return FL_RESULT_ERR;
    }
    rc = fl_wq_journal_mem()->scan(c, st, o, ex, after, max, out, n);
    if (rc == FL_RESULT_OK && *n && s_fj.after_scan)
        s_fj.after_scan();
    return rc;
}

static unsigned fj_count(void *c, fl_wq_state_t st, const char *o, uint32_t ex) {
    return fl_wq_journal_mem()->count(c, st, o, ex);
}

static void fj_install(void) {
    memset(&s_fj, 0, sizeof s_fj);
    s_fj.mark_result = FL_RESULT_ERR;
    s_fj_ops.ctx = NULL;
    s_fj_ops.append = fj_append;
    s_fj_ops.mark = fj_mark;
    s_fj_ops.load = fj_load;
    s_fj_ops.scan = fj_scan;
    s_fj_ops.count = fj_count;
    fl_wq_journal_mem_reset();
    (void)fl_wq_set_journal(&s_fj_ops);
    s_dur_ran = 0;
    s_dur_fail_left = 0;
}

static void fj_remove(void) {
    (void)fl_wq_set_journal(NULL);
}

static fl_wq_state_t rec_state(uint64_t id) {
    fl_wq_journal_record_t r;

    return fl_wq_journal_mem()->load(NULL, id, &r) == FL_RESULT_OK ? r.state : (fl_wq_state_t)0;
}

static void seed_record(uint64_t key_no, uint32_t executor, fl_wq_state_t to, const char *owner, uint8_t recover) {
    fl_wq_journal_record_t r;
    uint64_t id;
    int ex;

    memset(&r, 0, sizeof r);
    r.executor = executor;
    r.tag = TAG_AUTO;
    r.layer = FL_WQ_LAYER_NORMAL;
    r.recover = recover;
    r.max_attempts = FL_WQ_MAX_ATTEMPTS;
    mkkey(r.idem_key, (unsigned)key_no);
    if (owner)
        strcpy(r.owner, owner);
    if (fl_wq_journal_mem()->append(NULL, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) != FL_RESULT_OK)
        abort();
    if (to != FL_WQ_ST_PENDING && fl_wq_journal_mem()->mark(NULL, id, FL_WQ_ST_PENDING, to) != FL_RESULT_OK)
        abort();
}

/* A rival thread claims record 1 after this thread's scan returned it. */
static void rival_claims_record_one(void) {
    s_fj.after_scan = NULL;
    (void)fl_wq_journal_mem()->mark(NULL, 1, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED);
}

static int test_competing_staging_stages_once(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    fl_wq_journal_record_t r;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(3001, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_PENDING, NULL, FL_WQ_RECOVER_AUTO);
    s_fj.after_scan = rival_claims_record_one;
    /* the rival owns the claim: this attempt loses the compare-and-set and stages nothing */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0);
    ASSERT(st.durable_inmem == 0 && st.reserved == 0 && fl_wq_pending(wq) == 0);
    ASSERT(rec_state(1) == FL_WQ_ST_QUEUED && s_dur_ran == 0);
    /* a lost race is not an I/O failure: recovery sees a clean journal and the job runs once */
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1);
    ASSERT(fl_wq_journal_mem()->load(NULL, 1, &r) == FL_RESULT_OK && r.state == FL_WQ_ST_DONE);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static _Atomic unsigned s_threaded_ran;
static fl_result_t threaded_handler(const void *p, size_t n, const uint8_t k[16]) {
    (void)p; (void)n; (void)k;
    atomic_fetch_add(&s_threaded_ran, 1u);
    return FL_RESULT_OK;
}

static void *poll_until_idle(void *arg) {
    fl_workqueue_t *wq = (fl_workqueue_t *)arg;

    for (int i = 0; i < 2000; i++)
        (void)fl_wq_poll_at(wq, 8, 0);
    return NULL;
}

/* Two queues share one executor id here only to race on the same records; each job still runs once. */
static int test_two_pollers_never_double_stage(void) {
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    const unsigned jobs = 40;
    pthread_t ta, tb;

    ASSERT(fl_wq_register_handler(0x7003, threaded_handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_OK);
    fl_wq_journal_mem_reset();
    atomic_store(&s_threaded_ran, 0);
    ASSERT(dq_init(a) == FL_RESULT_OK && fl_wq_init(b) == FL_RESULT_OK);
    sb.executor = FL_WQ_EXECUTOR_DEFAULT;
    for (unsigned i = 0; i < jobs; i++) {
        uint8_t key[16];

        mkkey(key, 4000 + i);
        ASSERT(fl_wq_enqueue_durable(a, 0x7003, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    }
    ASSERT(pthread_create(&ta, NULL, poll_until_idle, a) == 0 && pthread_create(&tb, NULL, poll_until_idle, b) == 0);
    pthread_join(ta, NULL);
    pthread_join(tb, NULL);
    ASSERT(atomic_load(&s_threaded_ran) == jobs);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == jobs);
    ASSERT(sa.reserved == 0 && sb.reserved == 0);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    return 0;
}

static unsigned s_hook_accepted;
static fl_workqueue_t *s_hook_wq;

static void ordinary_noop(void *ctx) {
    (void)ctx;
}

/* While the durable claim holds its reservation, ordinary work takes every slot it is allowed. */
static void fill_ordinary_work(void) {
    fl_wq_work_t w = {.fn = ordinary_noop, .pq_layer = FL_WQ_LAYER_NORMAL};

    s_fj.after_scan = NULL;
    while (fl_wq_enqueue(s_hook_wq, &w) == FL_RESULT_OK)
        s_hook_accepted++;
}

static int test_reservation_is_counted_in_every_capacity_check(void) {
    fl_workqueue_storage_t sw;
    fl_workqueue_t *wq = (fl_workqueue_t *)&sw;
    fl_wq_work_t prot = {.fn = ordinary_noop, .pq_layer = FL_WQ_LAYER_NORMAL, .flags = FL_WQ_PROTECTED};
    unsigned reserve_taken = 0;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    s_hook_accepted = 0;
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    s_hook_wq = wq;
    mkkey(key, 3100);
    s_fj.after_scan = fill_ordinary_work;
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    /* the durable slot was promised before the ordinary work arrived, so it was not taken from it */
    ASSERT(s_hook_accepted == FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE - 1u);
    ASSERT(rec_state(1) == FL_WQ_ST_QUEUED);
    ASSERT(fl_wq_pending(wq) == (int)(FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE));
    while (fl_wq_enqueue(wq, &prot) == FL_RESULT_OK)
        reserve_taken++;
    ASSERT(reserve_taken == FL_WQ_PROTECTED_RESERVE && fl_wq_pending(wq) == (int)FL_WQ_PENDING_MAX);
    ASSERT(fl_wq_drain(wq, 2000) == FL_RESULT_OK); /* bounded: a leak of the slots fails here, not as a hang */
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* N claims in flight at once, each holding its reservation while it reads the journal: nested staging. */
static unsigned s_nest, s_max_reserved, s_nest_limit;

static void nest_reservations(void) {
    fl_workqueue_storage_t *st = (fl_workqueue_storage_t *)s_hook_wq;
    uint8_t key[16];

    if (st->reserved > s_max_reserved)
        s_max_reserved = st->reserved;
    if (s_nest >= s_nest_limit)
        return;
    mkkey(key, 7000 + s_nest++);
    (void)fl_wq_enqueue_durable(s_hook_wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0);
}

static int nested_staging_stays_within(unsigned ordinary, unsigned most_reserved) {
    fl_workqueue_storage_t sw;
    fl_workqueue_t *wq = (fl_workqueue_t *)&sw;
    fl_wq_work_t w = {.fn = ordinary_noop, .pq_layer = FL_WQ_LAYER_NORMAL};
    uint8_t key[16];

    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    for (unsigned i = 0; i < ordinary; i++)
        ASSERT(fl_wq_enqueue(wq, &w) == FL_RESULT_OK);
    s_hook_wq = wq;
    s_nest = 0;
    s_max_reserved = 0;
    s_nest_limit = 3u * FL_WQ_PENDING_MAX;
    s_fj.after_scan = nest_reservations;
    mkkey(key, 6900);
    (void)fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0);
    s_fj.after_scan = NULL;
    ASSERT(sw.reserved == 0);
    ASSERT(s_max_reserved <= most_reserved); /* in flight together: never more than room allows */
    ASSERT(sw.durable_inmem <= FL_WQ_PENDING_MAX / 2u);
    ASSERT(fl_wq_pending(wq) <= (int)(FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE));
    ASSERT(s_max_reserved >= most_reserved - 1u); /* and the test did press on the limit */
    ASSERT(fl_wq_drain(wq, 5000) == FL_RESULT_OK);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static int test_concurrent_claims_respect_the_window_and_the_cap(void) {
    ASSERT(register_dur_tags() == 0);
    /* empty queue: the 32-job window binds, whatever the cap would allow */
    ASSERT(nested_staging_stays_within(0, FL_WQ_PENDING_MAX / 2u) == 0);
    /* 40 ordinary items: the cap binds at 16 reservations, before the window does */
    ASSERT(nested_staging_stays_within(40, FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE - 40u) == 0);
    return 0;
}

static void shut_queue_after_claim(void) {
    ((fl_workqueue_storage_t *)s_hook_wq)->shutdown = 1;
}

/* Real fl_wq_shutdown while a claim still holds its reservation: wait on a second thread.
 * Do not spin on wq->st.shutdown unlocked (TSan); hand off through s_shutdown_armed under s_shut_mu
 * after the shutdown bit is written under the queue lock. */
static pthread_mutex_t s_shut_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_shut_cv = PTHREAD_COND_INITIALIZER;
static int s_claim_paused, s_shutdown_armed;

static void pause_claim_until_shutdown_flag(void) {
    pthread_mutex_lock(&s_shut_mu);
    s_claim_paused = 1;
    pthread_cond_broadcast(&s_shut_cv);
    while (!s_shutdown_armed)
        pthread_cond_wait(&s_shut_cv, &s_shut_mu);
    pthread_mutex_unlock(&s_shut_mu);
}

static void *call_shutdown(void *arg) {
    fl_workqueue_t *wq = (fl_workqueue_t *)arg;
    fl_workqueue_storage_t *st = (fl_workqueue_storage_t *)wq;

    pthread_mutex_lock(&s_shut_mu);
    while (!s_claim_paused)
        pthread_cond_wait(&s_shut_cv, &s_shut_mu);
    pthread_mutex_unlock(&s_shut_mu);
    /* Same bit fl_wq_shutdown sets, under the queue lock, so publish fails while reserved is held. */
    pthread_mutex_lock(&st->lock);
    st->shutdown = 1;
    pthread_mutex_unlock(&st->lock);
    pthread_mutex_lock(&s_shut_mu);
    s_shutdown_armed = 1;
    pthread_cond_broadcast(&s_shut_cv);
    pthread_mutex_unlock(&s_shut_mu);
    fl_wq_shutdown(wq); /* drains, then waits for this claim's reservation */
    return NULL;
}

/* resolve_held pins the queue as a filler; shutdown must wait until that fill finishes. */
static int s_filler_paused, s_filler_may_continue;

static void pause_while_resolve_filler(void) {
    fl_workqueue_storage_t *st = (fl_workqueue_storage_t *)s_hook_wq;

    /* HELD walk scans before fillers are pinned; only pause during the fill window. */
    if (!st || __atomic_load_n(&st->fillers, __ATOMIC_ACQUIRE) == 0)
        return;
    pthread_mutex_lock(&s_shut_mu);
    s_filler_paused = 1;
    pthread_cond_broadcast(&s_shut_cv);
    while (!s_filler_may_continue)
        pthread_cond_wait(&s_shut_cv, &s_shut_mu);
    pthread_mutex_unlock(&s_shut_mu);
}

/* Give fl_wq_shutdown time to reach wq_await_reservations while fillers stays > 0. Do not read
 * st->shutdown unlocked (TSan); nanosleep is enough because the pause holds the pin. */
static void *release_filler_after_brief_wait(void *arg) {
    struct timespec ts = {0, 100000000L}; /* 100 ms */

    (void)arg;
    (void)nanosleep(&ts, NULL);
    pthread_mutex_lock(&s_shut_mu);
    s_filler_may_continue = 1;
    pthread_cond_broadcast(&s_shut_cv);
    pthread_mutex_unlock(&s_shut_mu);
    return NULL;
}

static void *shutdown_while_resolve_fills(void *arg) {
    fl_workqueue_t *wq = (fl_workqueue_t *)arg;
    pthread_t releaser;

    pthread_mutex_lock(&s_shut_mu);
    while (!s_filler_paused)
        pthread_cond_wait(&s_shut_cv, &s_shut_mu);
    pthread_mutex_unlock(&s_shut_mu);
    if (pthread_create(&releaser, NULL, release_filler_after_brief_wait, NULL) != 0)
        return (void *)1;
    fl_wq_shutdown(wq);
    (void)pthread_join(releaser, NULL);
    return NULL;
}

static int test_shutdown_waits_for_resolve_fillers(void) {
    fl_workqueue_storage_t sw;
    fl_workqueue_t *wq = (fl_workqueue_t *)&sw;
    pthread_t th;
    fl_result_t rc;
    void *th_rc = NULL;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(3260, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_HELD, "Root.Fill", FL_WQ_RECOVER_ASK);
    s_hook_wq = wq;
    s_filler_paused = 0;
    s_filler_may_continue = 0;
    s_fj.after_scan = pause_while_resolve_filler;
    ASSERT(pthread_create(&th, NULL, shutdown_while_resolve_fills, wq) == 0);
    /* Fill may refuse to stage once shutdown is set; the pin wait is the acceptance point. */
    rc = fl_wq_resolve_held("Root.Fill", 1);
    ASSERT(rc == FL_RESULT_OK || rc == FL_RESULT_BUSY || rc == FL_RESULT_ERR);
    ASSERT(pthread_join(th, &th_rc) == 0 && th_rc == NULL);
    ASSERT(s_filler_paused == 1 && s_filler_may_continue == 1);
    ASSERT(sw.fillers == 0 && sw.reserved == 0);
    s_fj.after_scan = NULL;
    s_hook_wq = NULL;
    fj_remove();
    return 0;
}

/* fl_wq_shutdown runs while a claim still holds its reservation: it waits, the claim rolls back, then it returns. */
static int test_real_shutdown_waits_out_claim_rollback(void) {
    fl_workqueue_storage_t sw;
    fl_workqueue_t *wq = (fl_workqueue_t *)&sw;
    pthread_t th;
    uint8_t key[16];
    fl_result_t rc;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    s_hook_wq = wq;
    s_claim_paused = 0;
    s_shutdown_armed = 0;
    s_fj.on_claim = pause_claim_until_shutdown_flag;
    ASSERT(pthread_create(&th, NULL, call_shutdown, wq) == 0);
    mkkey(key, 3250);
    rc = fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0);
    ASSERT(rc == FL_WQ_ACCEPTED_UNSTAGED || rc == FL_RESULT_BUSY); /* shut mid-stage */
    ASSERT(pthread_join(th, NULL) == 0);
    ASSERT(sw.reserved == 0);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING); /* claim returned before shutdown finished waiting */
    s_fj.on_claim = NULL;
    fj_remove();
    /* queue already shut down by the helper thread */
    (void)rc;
    return 0;
}

/* Staging fails after the claim: the claim goes back, or, if that write fails too, it is reported. */
static int test_failed_staging_gives_the_claim_back(void) {
    fl_workqueue_storage_t sw;
    fl_workqueue_t *wq = (fl_workqueue_t *)&sw;
    fl_workqueue_storage_t *st = &sw;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    s_hook_wq = wq;
    mkkey(key, 3200);
    s_fj.on_claim = shut_queue_after_claim;
    /* accepted and durable, not staged: distinguishable from a rejection, and the job is PENDING again */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_WQ_ACCEPTED_UNSTAGED);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING && st->reserved == 0 && st->durable_inmem == 0);
    ASSERT(fl_wq_stuck_claims() == 0);
    st->shutdown = 0;
    s_fj.on_claim = NULL;
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1);

    mkkey(key, 3201);
    s_fj.on_claim = shut_queue_after_claim;
    s_fj.fail_to = FL_WQ_ST_PENDING; /* the give-back write fails too */
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_WQ_ACCEPTED_UNSTAGED);
    ASSERT(rec_state(2) == FL_WQ_ST_QUEUED && fl_wq_stuck_claims() == 1 && st->reserved == 0); /* reported, not lost */
    st->shutdown = 0;
    s_fj.on_claim = NULL;
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0 && s_dur_ran == 1); /* nothing stages a QUEUED record twice */
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK); /* recovery repairs the stuck claim */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 2 && rec_state(2) == FL_WQ_ST_DONE);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static int test_journal_failures_are_not_swallowed(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3300);
    s_fj.fail_append = 1; /* not durable: not accepted, and nothing was staged */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_ERR);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 0 && fl_wq_pending(wq) == 0);
    s_fj.fail_scan = 1; /* durable, staging cannot read the journal: accepted, and the next poll stages it */
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_WQ_ACCEPTED_UNSTAGED);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING);
    s_fj.fail_to = FL_WQ_ST_RUNNING; /* RUNNING cannot be written: the handler must not run */
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 0 && rec_state(1) == FL_WQ_ST_PENDING);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1 && rec_state(1) == FL_WQ_ST_DONE);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static int test_failed_completion_write_is_retried_not_rerun(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3400);
    s_fj.fail_to = FL_WQ_ST_DONE;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1);
    ASSERT(rec_state(1) == FL_WQ_ST_RUNNING && fl_wq_retry_marks_pending() == 1); /* the obligation is kept */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0); /* the next poll writes it; the effect does not repeat */
    ASSERT(rec_state(1) == FL_WQ_ST_DONE && fl_wq_retry_marks_pending() == 0 && s_dur_ran == 1);
    s_fj.mark_result = FL_RESULT_BUSY; /* a lost race on completion owes nothing */
    s_fj.fail_to = FL_WQ_ST_DONE;
    s_fj.fail_mark = 1;
    mkkey(key, 3401);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && fl_wq_retry_marks_pending() == 0);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* A retry entry names a row of the journal that was in force; another journal never sees it. */
static int test_installing_a_journal_drops_stale_retries(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3450);
    s_fj.fail_to = FL_WQ_ST_DONE;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && fl_wq_retry_marks_pending() == 1);
    (void)fl_wq_set_journal(&s_fj_ops);
    ASSERT(fl_wq_retry_marks_pending() == 0);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static int test_recovery_and_resolution_report_the_first_failure(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(3501, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_QUEUED, NULL, FL_WQ_RECOVER_AUTO);
    seed_record(3502, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_QUEUED, NULL, FL_WQ_RECOVER_AUTO);
    s_fj.fail_to = FL_WQ_ST_PENDING;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_recover(wq) == FL_RESULT_ERR); /* the first failure comes back ... */
    ASSERT(rec_state(1) == FL_WQ_ST_QUEUED && fl_wq_pending(wq) == 1); /* ... and the other record moved on */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && rec_state(2) == FL_WQ_ST_DONE);
    s_fj.mark_result = FL_RESULT_BUSY; /* a lost race is not a failure */
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK);

    seed_record(3503, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_HELD, "Root.Ann", FL_WQ_RECOVER_ASK);
    seed_record(3504, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_HELD, "Root.Ann", FL_WQ_RECOVER_ASK);
    s_fj.mark_result = FL_RESULT_ERR;
    s_fj.fail_to = FL_WQ_ST_SKIPPED;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_resolve_held("Root.Ann", 0) == FL_RESULT_ERR);
    ASSERT(fl_wq_held_count("Root.Ann") == 1 && fl_wq_journal_mem()->count(NULL, FL_WQ_ST_SKIPPED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* A failed PENDING->HELD triage must not let ASK work run on recover or on a later poll. */
static int test_failed_ask_triage_cannot_be_staged(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(3801, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_PENDING, "Root.Bob", FL_WQ_RECOVER_ASK);
    s_fj.mark_result = FL_RESULT_ERR;
    s_fj.fail_to = FL_WQ_ST_HELD;
    s_fj.fail_mark = 2; /* triage fails, and the fill/poll hold retry also fails once */
    ASSERT(fl_wq_recover(wq) == FL_RESULT_ERR);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING && s_dur_ran == 0); /* not staged, not run */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0 && s_dur_ran == 0); /* later poll must not bypass */
    ASSERT(rec_state(1) == FL_WQ_ST_HELD); /* the poll's second hold attempt lands */
    ASSERT(fl_wq_resolve_held("Root.Bob", 1) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* A failed load frees the staged slot and returns the claim; the failure is counted. */
static int test_failed_durable_load_returns_the_claim(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];
    unsigned before;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3901);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(rec_state(1) == FL_WQ_ST_QUEUED);
    before = fl_wq_durable_load_failures();
    s_fj.fail_load = 1;
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 0);
    ASSERT(fl_wq_durable_load_failures() == before + 1);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING); /* claim returned for restaging */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1 && rec_state(1) == FL_WQ_ST_DONE);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* A failed load whose claim return also fails is counted as a stuck claim (record stays QUEUED). */
static int test_failed_load_claim_return_is_counted(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];
    unsigned before_load, before_stuck;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3910);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    before_load = fl_wq_durable_load_failures();
    before_stuck = fl_wq_stuck_claims();
    s_fj.fail_load = 1;
    s_fj.mark_result = FL_RESULT_ERR;
    s_fj.fail_to = FL_WQ_ST_PENDING;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 0);
    ASSERT(fl_wq_durable_load_failures() == before_load + 1);
    ASSERT(fl_wq_stuck_claims() == before_stuck + 1);
    ASSERT(rec_state(1) == FL_WQ_ST_QUEUED); /* claim return failed; operator sees stuck_claims */
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* One executor's failed ASK triage must not gate or release another executor's ASK work. */
static int test_ask_gate_is_per_executor(void) {
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(fl_wq_init(a) == FL_RESULT_OK && fl_wq_set_executor(a, 11) == FL_RESULT_OK);
    ASSERT(fl_wq_init(b) == FL_RESULT_OK && fl_wq_set_executor(b, 22) == FL_RESULT_OK);
    seed_record(4301, 11, FL_WQ_ST_PENDING, "Root.A", FL_WQ_RECOVER_ASK);
    seed_record(4302, 22, FL_WQ_ST_PENDING, "Root.B", FL_WQ_RECOVER_ASK);
    seed_record(4303, 22, FL_WQ_ST_HELD, "Root.B", FL_WQ_RECOVER_ASK);
    s_fj.mark_result = FL_RESULT_ERR;
    s_fj.fail_to = FL_WQ_ST_HELD;
    s_fj.fail_mark = 2; /* A's recover triage and its fill hold retry both fail */
    ASSERT(fl_wq_recover(a) == FL_RESULT_ERR);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING && s_dur_ran == 0);
    /* B is not gated: its PENDING ASK may stage; A's remains unstaged. */
    ASSERT(fl_wq_poll_at(b, 8, 0) == 1 && s_dur_ran == 1);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING && rec_state(2) == FL_WQ_ST_DONE);
    /* Resolving B's held job must not clear A's gate or stage A's ASK. */
    s_dur_ran = 0;
    ASSERT(fl_wq_resolve_held("Root.B", 1) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(a, 8, 0) == 0 && s_dur_ran == 0);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING || rec_state(1) == FL_WQ_ST_HELD);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    fj_remove();
    return 0;
}

/* fl_wq_held_records reports a journal read error; fl_wq_held_count still has no error channel. */
static int test_held_records_reports_scan_failure(void) {
    unsigned n = 0;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    seed_record(4010, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_HELD, "Root.Cy", FL_WQ_RECOVER_ASK);
    ASSERT(fl_wq_held_count("Root.Cy") == 1);
    ASSERT(fl_wq_held_records("Root.Cy", &n) == FL_RESULT_OK && n == 1);
    s_fj.fail_scan = 1;
    ASSERT(fl_wq_held_records("Root.Cy", &n) == FL_RESULT_ERR);
    fj_remove();
    return 0;
}

/* resolve_held(run_now) keeps committed transitions and reports a staging failure. */
static int test_resolve_held_reports_staging_failure(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    unsigned before;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(4001, FL_WQ_EXECUTOR_DEFAULT, FL_WQ_ST_HELD, "Root.Cy", FL_WQ_RECOVER_ASK);
    before = fl_wq_staging_errors();
    s_fj.fail_to = FL_WQ_ST_QUEUED; /* claim after HELD->PENDING fails */
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_resolve_held("Root.Cy", 1) == FL_RESULT_ERR);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING); /* the resolve itself committed */
    ASSERT(fl_wq_staging_errors() == before + 1);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* More completion repairs than the 16-entry table: the rest are counted as lost; records stay RUNNING. */
static int test_retry_table_saturation_is_counted(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];
    unsigned before_lost, before_pending, n = 20;

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    before_lost = fl_wq_retry_marks_lost();
    before_pending = fl_wq_retry_marks_pending();
    s_fj.fail_to = FL_WQ_ST_DONE;
    s_fj.fail_mark = n; /* every completion write fails once */
    for (unsigned i = 0; i < n; i++) {
        mkkey(key, 4100 + i);
        ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    }
    ASSERT(fl_wq_poll_at(wq, n, 0) == n && s_dur_ran == n);
    ASSERT(fl_wq_retry_marks_pending() == before_pending + 16);
    ASSERT(fl_wq_retry_marks_lost() == before_lost + (n - 16));
    /* A later poll repairs the retained ones; the lost ones stay RUNNING for recovery. */
    ASSERT(fl_wq_poll_at(wq, 1, 0) == 0);
    ASSERT(fl_wq_retry_marks_pending() == 0);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_RUNNING, NULL, FL_WQ_EXECUTOR_ANY) == n - 16);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == 16);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

/* After the retry table is wiped (a new journal), recover re-runs RUNNING work that never completed. */
static int test_recover_reruns_when_completion_repairs_were_lost(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fj_install();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 4200);
    s_fj.fail_to = FL_WQ_ST_DONE;
    s_fj.fail_mark = 1;
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1 && rec_state(1) == FL_WQ_ST_RUNNING);
    ASSERT(fl_wq_retry_marks_pending() == 1);
    (void)fl_wq_set_journal(&s_fj_ops); /* drops the in-memory repair table */
    ASSERT(fl_wq_retry_marks_pending() == 0 && rec_state(1) == FL_WQ_ST_RUNNING);
    s_dur_ran = 0;
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK); /* RUNNING -> PENDING then staged */
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 1 && rec_state(1) == FL_WQ_ST_DONE);
    fl_wq_shutdown(wq);
    fj_remove();
    return 0;
}

static int test_executor_registry_is_bounded(void) {
    fl_workqueue_storage_t st[FL_WQ_EXECUTORS_MAX + 2];
    unsigned n = 0;
    fl_result_t rc = FL_RESULT_OK;

    while (n < FL_WQ_EXECUTORS_MAX + 2) {
        ASSERT(fl_wq_init((fl_workqueue_t *)&st[n]) == FL_RESULT_OK);
        rc = fl_wq_set_executor((fl_workqueue_t *)&st[n], 200u + n);
        if (rc == FL_RESULT_NOMEM)
            break;
        ASSERT(rc == FL_RESULT_OK);
        n++;
    }
    ASSERT(rc == FL_RESULT_NOMEM && n >= 1 && n <= FL_WQ_EXECUTORS_MAX);
    for (unsigned i = 0; i <= n; i++)
        fl_wq_shutdown((fl_workqueue_t *)&st[i]);
    return 0;
}

static int test_executors_do_not_share_records(void) {
    fl_workqueue_storage_t sa, sb, sc;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb, *none = (fl_workqueue_t *)&sc;
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    ASSERT(dq_init(a) == FL_RESULT_OK && fl_wq_init(b) == FL_RESULT_OK && fl_wq_init(none) == FL_RESULT_OK);
    ASSERT(fl_wq_set_executor(b, 2) == FL_RESULT_OK && fl_wq_set_executor(b, FL_WQ_EXECUTOR_NONE) == FL_RESULT_INVAL);
    ASSERT(fl_wq_set_executor(b, FL_WQ_EXECUTOR_ANY) == FL_RESULT_INVAL);
    mkkey(key, 3600);
    ASSERT(fl_wq_enqueue_durable(none, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_INVAL);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    mkkey(key, 3601);
    ASSERT(fl_wq_enqueue_durable(b, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(j->count(NULL, FL_WQ_ST_QUEUED, NULL, 1) == 1 && j->count(NULL, FL_WQ_ST_QUEUED, NULL, 2) == 1);
    ASSERT(fl_wq_poll_at(none, 8, 0) == 0 && s_dur_ran == 0); /* an unconfigured queue consumes nothing */
    ASSERT(fl_wq_recover(none) == FL_RESULT_OK && j->count(NULL, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 2);
    ASSERT(fl_wq_poll_at(b, 8, 0) == 1 && s_dur_ran == 1 && rec_state(2) == FL_WQ_ST_DONE && rec_state(1) == FL_WQ_ST_QUEUED);
    ASSERT(fl_wq_poll_at(a, 8, 0) == 1 && rec_state(1) == FL_WQ_ST_DONE);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    fl_wq_shutdown(none);
    return 0;
}

/* A queue staged a record, then its executor changed: it hands the record back instead of running it. */
static int test_a_staged_record_of_another_executor_is_not_run(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    mkkey(key, 3650);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_set_executor(wq, 7) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 1 && s_dur_ran == 0 && rec_state(1) == FL_WQ_ST_PENDING);
    fl_wq_shutdown(wq);
    return 0;
}

/* Keys are global: another executor repeating a key is told about the existing job, nothing is added. */
static int test_a_repeated_key_is_answered_across_executors(void) {
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    uint8_t key[16];

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    ASSERT(dq_init(a) == FL_RESULT_OK && fl_wq_init(b) == FL_RESULT_OK && fl_wq_set_executor(b, 2) == FL_RESULT_OK);
    mkkey(key, 3660);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(b, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    ASSERT(fl_wq_journal_mem()->count(NULL, FL_WQ_ST_QUEUED, NULL, 2) == 0 && fl_wq_pending(b) == 0);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    return 0;
}

static int test_unknown_executor_is_left_alone_and_reported(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    unsigned unowned = 99;

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    s_dur_ran = 0;
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    seed_record(3701, 99, FL_WQ_ST_PENDING, NULL, FL_WQ_RECOVER_AUTO);
    seed_record(3702, 99, FL_WQ_ST_HELD, "Root.Zed", FL_WQ_RECOVER_ASK);
    seed_record(3703, 99, FL_WQ_ST_QUEUED, NULL, FL_WQ_RECOVER_AUTO);
    ASSERT(fl_wq_recover(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(wq, 8, 0) == 0 && s_dur_ran == 0);
    ASSERT(rec_state(1) == FL_WQ_ST_PENDING && rec_state(3) == FL_WQ_ST_QUEUED); /* recovery left them */
    ASSERT(fl_wq_resolve_held("Root.Zed", 1) == FL_RESULT_OK);
    ASSERT(rec_state(2) == FL_WQ_ST_HELD && fl_wq_poll_at(wq, 8, 0) == 0); /* no queue owns it: it stays held */
    ASSERT(fl_wq_unowned_records(&unowned) == FL_RESULT_OK && unowned == 3);
    ASSERT(fl_wq_set_executor(wq, 99) == FL_RESULT_OK); /* once an executor is registered they are its own */
    ASSERT(fl_wq_unowned_records(&unowned) == FL_RESULT_OK && unowned == 0);
    ASSERT(fl_wq_resolve_held("Root.Zed", 1) == FL_RESULT_OK && fl_wq_poll_at(wq, 8, 0) >= 1);
    fl_wq_shutdown(wq);
    return 0;
}

/* The bound counts PENDING, QUEUED, RUNNING and HELD, and a repeat is answered before it is applied. */
static int test_admission_counts_every_nonterminal_state(void) {
    const fl_wq_journal_ops_t *j = fl_wq_journal_mem();
    fl_wq_journal_record_t r;
    uint64_t id[4], again;
    int ex;

    fl_wq_journal_mem_reset();
    memset(&r, 0, sizeof r);
    r.executor = FL_WQ_EXECUTOR_DEFAULT; r.tag = TAG_AUTO; r.layer = FL_WQ_LAYER_NORMAL; r.max_attempts = 3;
    for (unsigned i = 0; i < 4; i++) {
        mkkey(r.idem_key, 3800 + i);
        ASSERT(j->append(NULL, &r, 4, &id[i], &ex) == FL_RESULT_OK && !ex);
    }
    ASSERT(j->mark(NULL, id[1], FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_OK);
    ASSERT(j->mark(NULL, id[1], FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING) == FL_RESULT_OK);
    ASSERT(j->mark(NULL, id[2], FL_WQ_ST_PENDING, FL_WQ_ST_HELD) == FL_RESULT_OK);
    mkkey(r.idem_key, 3900);
    ASSERT(j->append(NULL, &r, 4, &again, &ex) == FL_RESULT_BUSY); /* PENDING 2, RUNNING 1, HELD 1: full */
    mkkey(r.idem_key, 3800);
    ASSERT(j->append(NULL, &r, 4, &again, &ex) == FL_RESULT_OK && ex && again == id[0]); /* the repeat wins */
    ASSERT(j->mark(NULL, id[3], FL_WQ_ST_PENDING, FL_WQ_ST_EXPIRED) == FL_RESULT_OK);
    mkkey(r.idem_key, 3900);
    ASSERT(j->append(NULL, &r, 4, &again, &ex) == FL_RESULT_OK && !ex); /* a finished job frees its place */
    return 0;
}

static int test_full_journal_still_answers_a_repeat(void) {
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    uint8_t key[16];
    unsigned n = 0;

    ASSERT(register_dur_tags() == 0);
    fl_wq_journal_mem_reset();
    ASSERT(dq_init(wq) == FL_RESULT_OK);
    for (;; n++) {
        mkkey(key, 6000 + n);
        key[3] = (uint8_t)(n >> 8);
        if (fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_IDLE, NULL, NULL, 0, key, 0) == FL_RESULT_BUSY)
            break;
        ASSERT(n < 2 * FL_WQ_JOURNAL_MAX_PENDING);
    }
    ASSERT(n == FL_WQ_JOURNAL_MAX_PENDING); /* staged (QUEUED) jobs count toward the bound too */
    mkkey(key, 6000);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_IDLE, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    mkkey(key, 9000);
    ASSERT(fl_wq_enqueue_durable(wq, TAG_AUTO, FL_WQ_LAYER_IDLE, NULL, NULL, 0, key, 0) == FL_RESULT_BUSY);
    fl_wq_shutdown(wq);
    return 0;
}

int main(void) {
    RUN_TEST(churn_1000);
    RUN_TEST(storage_is_reusable_after_destroy);
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
    RUN_TEST(durable_runs_once_and_is_idempotent);
    RUN_TEST(durable_owner_bound);
    RUN_TEST(durable_ttl_overflow);
    RUN_TEST(durable_retries_then_fails);
    RUN_TEST(durable_recovers_after_a_restart);
    RUN_TEST(stale_durable_work_expires_unrun);
    RUN_TEST(held_work_waits_for_the_owner);
    RUN_TEST(durable_backlog_is_bounded_and_drains);
    RUN_TEST(competing_staging_stages_once);
    RUN_TEST(two_pollers_never_double_stage);
    RUN_TEST(reservation_is_counted_in_every_capacity_check);
    RUN_TEST(shutdown_waits_for_resolve_fillers);
    RUN_TEST(real_shutdown_waits_out_claim_rollback);
    RUN_TEST(failed_staging_gives_the_claim_back);
    RUN_TEST(journal_failures_are_not_swallowed);
    RUN_TEST(failed_completion_write_is_retried_not_rerun);
    RUN_TEST(installing_a_journal_drops_stale_retries);
    RUN_TEST(recovery_and_resolution_report_the_first_failure);
    RUN_TEST(failed_ask_triage_cannot_be_staged);
    RUN_TEST(failed_durable_load_returns_the_claim);
    RUN_TEST(failed_load_claim_return_is_counted);
    RUN_TEST(ask_gate_is_per_executor);
    RUN_TEST(held_records_reports_scan_failure);
    RUN_TEST(resolve_held_reports_staging_failure);
    RUN_TEST(retry_table_saturation_is_counted);
    RUN_TEST(recover_reruns_when_completion_repairs_were_lost);
    RUN_TEST(executor_registry_is_bounded);
    RUN_TEST(concurrent_claims_respect_the_window_and_the_cap);
    RUN_TEST(a_staged_record_of_another_executor_is_not_run);
    RUN_TEST(a_repeated_key_is_answered_across_executors);
    RUN_TEST(executors_do_not_share_records);
    RUN_TEST(unknown_executor_is_left_alone_and_reported);
    RUN_TEST(admission_counts_every_nonterminal_state);
    RUN_TEST(full_journal_still_answers_a_repeat);
    RUN_TEST(model_table_is_pinned);
    RUN_TEST(hosted_capacity_and_lock_are_pinned);
    RUN_TEST(ticker_and_default_queue); /* last: it shuts the default queue down */
    RUN_TEST(the_queue_never_left_the_model);
    printf("test_workqueue_p18: all passed\n");
    return 0;
}
