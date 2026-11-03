#include "bg_jobs.h"

#include "net_background.h"
#include "workqueue.h"

#include <time.h>

static void bg_reclaim_work(void *ctx) {
    (void)ctx;
    /* P1-9: scan **P1-4** PMM / **P1-5** arenas — not implemented. */
}

static void bg_watchdog_work(void *ctx) {
    (void)ctx;
    /* P1-10: heartbeat / stall detection — not implemented. */
}

static int s_bg_inited;

void fl_bg_jobs_init(void) {
    if (s_bg_inited)
        return;
    (void)fl_wq_default();
    s_bg_inited = 1;
}

void fl_bg_jobs_shutdown(void) {
    if (!s_bg_inited)
        return;
    fl_wq_shutdown(fl_wq_default());
    s_bg_inited = 0;
}

fl_result_t fl_bg_job_reclaim_kick(void) {
    fl_wq_work_t w = {
        .fn = bg_reclaim_work,
        .ctx = NULL,
        .tag = FL_BG_JOB_TAG_RECLAIM,
        .pq_layer = FL_WQ_LAYER_BACKGROUND,
    };
    return fl_wq_enqueue(fl_wq_default(), &w);
}

fl_result_t fl_bg_job_watchdog_kick(void) {
    fl_wq_work_t w = {
        .fn = bg_watchdog_work,
        .ctx = NULL,
        .tag = FL_BG_JOB_TAG_WATCHDOG,
        .pq_layer = FL_WQ_LAYER_URGENT,
    };
    return fl_wq_enqueue(fl_wq_default(), &w);
}

/* One tick driver, bounded work per tick. Delayed and periodic work (cron, the key service's
 * watchdog) are workqueue items added with fl_wq_enqueue_at / fl_wq_enqueue_every; fl_wq_poll
 * moves the due ones into the MLQ first, so this stays one poll and needs no extra call per user. */
void fl_bg_jobs_tick(unsigned max_wq_items) {
    if (!s_bg_inited)
        fl_bg_jobs_init();
    (void)fl_wq_poll(fl_wq_default(), max_wq_items);
    (void)fl_net_background_tick(max_wq_items);
}

#ifdef FL_WQ_HAS_LOCK
/* Hosted tick source: the ONLY thread that polls the default queue. */
static pthread_t s_ticker;
static int s_ticker_running;
static volatile int s_ticker_stop;
static unsigned s_ticker_period_ms;

static void *bg_ticker_main(void *arg) {
    (void)arg;
    while (!s_ticker_stop) {
        struct timespec ts;

        fl_bg_jobs_tick(FL_BG_TICK_MAX_ITEMS);
        ts.tv_sec = (time_t)(s_ticker_period_ms / 1000u);
        ts.tv_nsec = (long)(s_ticker_period_ms % 1000u) * 1000000L;
        (void)nanosleep(&ts, NULL);
    }
    return NULL;
}

fl_result_t fl_bg_jobs_ticker_start(unsigned period_ms) {
    if (period_ms == 0)
        return FL_RESULT_INVAL;
    if (s_ticker_running)
        return FL_RESULT_BUSY;
    if (!s_bg_inited)
        fl_bg_jobs_init();
    s_ticker_period_ms = period_ms;
    s_ticker_stop = 0;
    if (pthread_create(&s_ticker, NULL, bg_ticker_main, NULL) != 0)
        return FL_RESULT_ERR;
    s_ticker_running = 1;
    return FL_RESULT_OK;
}

void fl_bg_jobs_ticker_stop(void) {
    if (!s_ticker_running)
        return;
    s_ticker_stop = 1;
    (void)pthread_join(s_ticker, NULL);
    s_ticker_running = 0;
}
#endif
