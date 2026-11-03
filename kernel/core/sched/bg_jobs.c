#include "bg_jobs.h"

#include "net_background.h"
#include "workqueue.h"

#include <stdatomic.h>
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
/* Hosted tick source: the ONLY thread that polls the default queue (a second poller trips the
 * nested-poll assert). fl_bg_jobs_ticker_start / _stop and fl_bg_jobs_shutdown are single-thread
 * calls: the shell's main thread makes them. */
static pthread_t s_ticker;
static int s_ticker_running;
static _Atomic int s_ticker_stop;
static unsigned s_ticker_period_ms;
static pthread_mutex_t s_ticker_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_ticker_cv;

static void *bg_ticker_main(void *arg) {
    (void)arg;
    while (!atomic_load(&s_ticker_stop)) {
        struct timespec dl;

        fl_bg_jobs_tick(FL_BG_TICK_MAX_ITEMS);
        (void)clock_gettime(CLOCK_MONOTONIC, &dl);
        dl.tv_sec += (time_t)(s_ticker_period_ms / 1000u);
        dl.tv_nsec += (long)(s_ticker_period_ms % 1000u) * 1000000L;
        if (dl.tv_nsec >= 1000000000L) {
            dl.tv_sec++;
            dl.tv_nsec -= 1000000000L;
        }
        /* stop sets the flag, then takes the lock and signals: checking under the lock loses no wakeup */
        pthread_mutex_lock(&s_ticker_mu);
        while (!atomic_load(&s_ticker_stop) &&
               pthread_cond_timedwait(&s_ticker_cv, &s_ticker_mu, &dl) == 0)
            ;
        pthread_mutex_unlock(&s_ticker_mu);
    }
    return NULL;
}

fl_result_t fl_bg_jobs_ticker_start(unsigned period_ms) {
    pthread_condattr_t ca;

    if (period_ms == 0)
        return FL_RESULT_INVAL;
    if (s_ticker_running)
        return FL_RESULT_BUSY;
    if (!s_bg_inited)
        fl_bg_jobs_init();
    /* Run the lazy net init here, on the calling thread, so the ticker and the caller never race it. */
    fl_net_background_init();
    s_ticker_period_ms = period_ms;
    atomic_store(&s_ticker_stop, 0);
    if (pthread_condattr_init(&ca) != 0)
        return FL_RESULT_ERR;
    (void)pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    (void)pthread_cond_init(&s_ticker_cv, &ca);
    pthread_condattr_destroy(&ca);
    if (pthread_create(&s_ticker, NULL, bg_ticker_main, NULL) != 0) {
        pthread_cond_destroy(&s_ticker_cv);
        return FL_RESULT_ERR;
    }
    s_ticker_running = 1;
    return FL_RESULT_OK;
}

void fl_bg_jobs_ticker_stop(void) {
    if (!s_ticker_running)
        return;
    atomic_store(&s_ticker_stop, 1);
    pthread_mutex_lock(&s_ticker_mu);
    pthread_cond_signal(&s_ticker_cv);
    pthread_mutex_unlock(&s_ticker_mu);
    (void)pthread_join(s_ticker, NULL);
    pthread_cond_destroy(&s_ticker_cv);
    s_ticker_running = 0;
}
#endif
