#include "bg_jobs.h"

#include "net_background.h"
#include "workqueue.h"

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
    (void)fl_wq_init(fl_wq_default());
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

void fl_bg_jobs_tick(unsigned max_wq_items) {
    if (!s_bg_inited)
        fl_bg_jobs_init();
    (void)fl_wq_poll(fl_wq_default(), max_wq_items);
    (void)fl_net_background_tick(max_wq_items);
}
