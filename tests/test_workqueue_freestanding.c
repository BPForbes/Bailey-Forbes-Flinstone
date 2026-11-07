/*
 * The workqueue in its freestanding configuration (-DFL_FREESTANDING: no lock, static item pool, no
 * libc heap), exercised on the host. The browser image compiles the same kernel/core/sched sources;
 * this proves the pool hands every item back (a leak would show as BUSY/NOMEM after a few cycles) and
 * that the tick-driven clock path (fl_wq_poll_at) behaves as the hosted one does.
 */
#include "workqueue.h"

#include <stdio.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

#ifndef FL_FREESTANDING
#error "this test is for the FL_FREESTANDING configuration"
#endif
#ifdef FL_WQ_HAS_LOCK
#error "the freestanding configuration must not carry a lock"
#endif

/* The image's clock is the PIT tick count; here a settable counter. */
static int64_t s_now_ns;
fl_result_t fl_time_monotonic_ns(int64_t *out) { *out = s_now_ns; return FL_RESULT_OK; }

static unsigned s_ran;
static void count(void *ctx) { (void)ctx; s_ran++; }

static int test_pool_recycles(void)
{
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    fl_wq_work_t w = {.fn = count, .pq_layer = FL_WQ_LAYER_NORMAL};
    fl_wq_work_t t = {.fn = count, .pq_layer = FL_WQ_LAYER_NORMAL};
    unsigned n;

    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    for (int cycle = 0; cycle < 20; cycle++) {
        n = 0;
        while (fl_wq_enqueue(wq, &w) == FL_RESULT_OK)
            n++;
        ASSERT(n == FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE);
        ASSERT(fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, 0) == n);
        ASSERT(fl_wq_pending(wq) == 0);
    }
    /* timers draw from the same pool */
    for (int cycle = 0; cycle < 5; cycle++) {
        for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
            ASSERT(fl_wq_enqueue_at(wq, &t, 100 + i) == FL_RESULT_OK);
        ASSERT(fl_wq_enqueue_at(wq, &t, 100) == FL_RESULT_BUSY);
        ASSERT(fl_wq_poll_at(wq, FL_WQ_PENDING_MAX, 1000) == FL_WQ_TIMERS_MAX);
    }
    fl_wq_shutdown(wq);
    /* shutdown returned every item: a fresh queue can fill to its cap again */
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    n = 0;
    while (fl_wq_enqueue(wq, &w) == FL_RESULT_OK)
        n++;
    ASSERT(n == FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE);
    fl_wq_shutdown(wq);
    return 0;
}

static int test_periodic_on_a_tick_clock(void)
{
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    fl_wq_work_t p = {.fn = count, .pq_layer = FL_WQ_LAYER_NORMAL, .flags = FL_WQ_PROTECTED};

    s_ran = 0;
    s_now_ns = 0;
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_every(wq, &p, 500000000ll) == FL_RESULT_OK); /* due at 0.5 s */
    for (int tick = 1; tick <= 300; tick++) { /* 3 s of 10 ms ticks */
        s_now_ns = (int64_t)tick * 10000000ll;
        (void)fl_wq_poll(wq, 8);
    }
    ASSERT(s_ran >= 5 && s_ran <= 6);
    fl_wq_shutdown(wq); /* protected: runs once more, then the item returns to the pool */
    ASSERT(s_ran >= 6 && s_ran <= 7);
    return 0;
}

static fl_result_t dur_handler(const void *p, size_t n, const uint8_t k[16])
{
    (void)p; (void)n; (void)k;
    s_ran++;
    return FL_RESULT_OK;
}

/* the image's journal is memory only and small: durable work works, a full journal says BUSY */
static int test_durable_in_the_image(void)
{
    fl_workqueue_storage_t st;
    fl_workqueue_t *wq = (fl_workqueue_t *)&st;
    unsigned n = 0;
    uint8_t key[16] = {0};

    s_ran = 0;
    s_now_ns = 0;
    ASSERT(fl_wq_register_handler(0x7301, dur_handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_OK);
    ASSERT(fl_wq_init(wq) == FL_RESULT_OK && fl_wq_set_executor(wq, FL_WQ_EXECUTOR_DEFAULT) == FL_RESULT_OK);
    for (;; n++) {
        key[0] = (uint8_t)(n + 1);
        if (fl_wq_enqueue_durable(wq, 0x7301, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_BUSY)
            break;
        ASSERT(n < 64);
    }
    ASSERT(n == 16);
    for (int i = 0; i < 8; i++)
        (void)fl_wq_poll_at(wq, 8, 0);
    ASSERT(s_ran == 16);
    fl_wq_shutdown(wq);
    return 0;
}

/* no lock, no threads: a queue with no executor consumes nothing, and a lost claim stages nothing twice */
static int test_executor_isolation_in_the_image(void)
{
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    uint8_t key[16] = {0x42};

    s_ran = 0;
    s_now_ns = 0;
    fl_wq_journal_mem_reset();
    ASSERT(fl_wq_init(a) == FL_RESULT_OK && fl_wq_init(b) == FL_RESULT_OK);
    ASSERT(fl_wq_set_executor(a, FL_WQ_EXECUTOR_DEFAULT) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(b, 0x7301, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_INVAL);
    ASSERT(fl_wq_enqueue_durable(a, 0x7301, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, key, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(b, 8, 0) == 0 && s_ran == 0);
    ASSERT(sa.reserved == 0 && sa.durable_inmem == 1);
    ASSERT(fl_wq_poll_at(a, 8, 0) == 1 && s_ran == 1 && sa.durable_inmem == 0);
    fl_wq_shutdown(a);
    fl_wq_shutdown(b);
    return 0;
}

int main(void)
{
    if (test_pool_recycles() || test_periodic_on_a_tick_clock() || test_durable_in_the_image() ||
        test_executor_isolation_in_the_image())
        return 1;
    printf("test_workqueue_freestanding: all passed\n");
    return 0;
}
