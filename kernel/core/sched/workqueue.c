#include "workqueue.h"

#include "timekeeping.h"

#include <assert.h>
#include <stdlib.h>

#ifndef NDEBUG
#define FL_WQ_DEBUG_REENTRANCY 1
#endif

typedef struct wq_item {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag;
    int layer;
    int64_t run_at_ns;
    int64_t period_ns;
    uint32_t flags;
    struct fl_workqueue *owner;
} wq_item_t;

/* The queue is its storage type, so the two can never drift apart. */
struct fl_workqueue {
    fl_workqueue_storage_t st;
};

static fl_workqueue_t s_default_wq;
#ifdef FL_WQ_HAS_LOCK
static pthread_once_t s_default_once = PTHREAD_ONCE_INIT;
#else
static int s_default_inited;
#endif

#ifdef FL_WQ_HAS_LOCK
#define WQ_LOCK(wq) (void)pthread_mutex_lock(&(wq)->st.lock)
#define WQ_UNLOCK(wq) (void)pthread_mutex_unlock(&(wq)->st.lock)
#else
#define WQ_LOCK(wq) ((void)0)
#define WQ_UNLOCK(wq) ((void)0)
#endif

static wq_item_t *wq_timer_at(const fl_workqueue_t *wq, unsigned i) {
    return (wq_item_t *)wq->st.timers[i];
}

/* Keep timers ordered by due time; equal times keep arrival order. Caller holds the lock
 * and has checked there is room. */
static void wq_timer_insert(fl_workqueue_t *wq, wq_item_t *item) {
    unsigned i = wq->st.timer_count;

    while (i > 0 && wq_timer_at(wq, i - 1)->run_at_ns > item->run_at_ns) {
        wq->st.timers[i] = wq->st.timers[i - 1];
        i--;
    }
    wq->st.timers[i] = item;
    wq->st.timer_count++;
}

static void wq_timer_remove_head(fl_workqueue_t *wq) {
    for (unsigned i = 1; i < wq->st.timer_count; i++)
        wq->st.timers[i - 1] = wq->st.timers[i];
    wq->st.timer_count--;
    wq->st.timers[wq->st.timer_count] = NULL;
}

/* Runs on the poll thread, with the lock released. A periodic item goes back to the timer
 * array (its slot was kept in periodic_inflight, so this cannot fail); anything else is freed. */
static void wq_dispatch(void *arg) {
    wq_item_t *item = (wq_item_t *)arg;
    fl_workqueue_t *wq;

    if (!item)
        return;
    wq = item->owner;
    if (item->fn)
        item->fn(item->ctx);
    if (item->period_ns <= 0) {
        free(item);
        return;
    }
    WQ_LOCK(wq);
    wq->st.periodic_inflight--;
    if (wq->st.shutdown) {
        WQ_UNLOCK(wq);
        free(item);
        return;
    }
    {
        int64_t base = wq->st.poll_now_ns >= 0 ? wq->st.poll_now_ns : item->run_at_ns;
        item->run_at_ns = base + item->period_ns;
    }
    wq_timer_insert(wq, item);
    WQ_UNLOCK(wq);
}

static int wq_resolve_layer(const fl_wq_work_t *work) {
    int layer = FL_WQ_LAYER_BACKGROUND;

    if (!work)
        return layer;
    if (work->pq_layer >= 0 && work->pq_layer < PQ_NUM_PRIORITIES)
        layer = work->pq_layer;
    return layer;
}

static void wq_do_init(fl_workqueue_t *wq) {
    pq_init(&wq->st.pq);
    wq->st.shutdown = 0;
    wq->st.poll_depth = 0;
    wq->st.timer_count = 0;
    wq->st.periodic_inflight = 0;
    wq->st.poll_now_ns = -1;
    for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
        wq->st.timers[i] = NULL;
#ifdef FL_WQ_HAS_LOCK
    /* Not destroyed at shutdown: the queue may be re-initialised in place, and a held-open
     * mutex object owns no resource on the platforms this builds for. */
    (void)pthread_mutex_init(&wq->st.lock, NULL);
#endif
}

#ifdef FL_WQ_HAS_LOCK
static void wq_default_init_once(void) {
    wq_do_init(&s_default_wq);
}
#endif

fl_workqueue_t *fl_wq_default(void) {
#ifdef FL_WQ_HAS_LOCK
    (void)pthread_once(&s_default_once, wq_default_init_once);
#else
    if (!s_default_inited) {
        wq_do_init(&s_default_wq);
        s_default_inited = 1;
    }
#endif
    return &s_default_wq;
}

fl_result_t fl_wq_init(fl_workqueue_t *wq) {
    if (!wq)
        return FL_RESULT_INVAL;
    wq_do_init(wq);
    return FL_RESULT_OK;
}

/* Shutdown path: protected work runs once, everything else is dropped. */
static void wq_finish(wq_item_t *item) {
    if (item->fn && (item->flags & FL_WQ_PROTECTED))
        item->fn(item->ctx);
    free(item);
}

void fl_wq_shutdown(fl_workqueue_t *wq) {
    pq_task_t task;

    if (!wq)
        return;
    WQ_LOCK(wq);
    wq->st.shutdown = 1;
    while (!pq_is_empty(&wq->st.pq)) {
        if (pq_pop(&wq->st.pq, &task) != 0)
            break;
        WQ_UNLOCK(wq);
        if (task.arg)
            wq_finish((wq_item_t *)task.arg);
        WQ_LOCK(wq);
    }
    while (wq->st.timer_count > 0) {
        wq_item_t *item = wq_timer_at(wq, 0);

        wq_timer_remove_head(wq);
        WQ_UNLOCK(wq);
        wq_finish(item);
        WQ_LOCK(wq);
    }
    WQ_UNLOCK(wq);
}

int fl_wq_pending(const fl_workqueue_t *wq) {
    int n;

    if (!wq)
        return 0;
    WQ_LOCK((fl_workqueue_t *)wq);
    n = pq_count(&wq->st.pq) + (int)wq->st.timer_count;
    WQ_UNLOCK((fl_workqueue_t *)wq);
    return n;
}

static fl_result_t wq_submit(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns,
                             int64_t period_ns) {
    wq_item_t *item;
    unsigned limit;

    if (!wq || !work || !work->fn || run_at_ns < 0 || period_ns < 0)
        return FL_RESULT_INVAL;
    if (period_ns > 0 && run_at_ns == 0) {
        int64_t now = 0;

        if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
            return FL_RESULT_ERR; /* a period needs P1-7 */
        run_at_ns = now + period_ns;
    }

    item = (wq_item_t *)calloc(1, sizeof(*item));
    if (!item)
        return FL_RESULT_NOMEM;
    item->fn = work->fn;
    item->ctx = work->ctx;
    item->tag = work->tag;
    item->layer = wq_resolve_layer(work);
    item->run_at_ns = run_at_ns;
    item->period_ns = period_ns;
    item->flags = work->flags;
    item->owner = wq;

    limit = FL_WQ_PENDING_MAX - ((work->flags & FL_WQ_PROTECTED) ? 0u : FL_WQ_PROTECTED_RESERVE);

    WQ_LOCK(wq);
    if (wq->st.shutdown || (unsigned)pq_count(&wq->st.pq) + wq->st.timer_count >= limit) {
        WQ_UNLOCK(wq);
        free(item);
        return FL_RESULT_BUSY;
    }
    if (run_at_ns == 0) {
        if (pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0) {
            WQ_UNLOCK(wq);
            free(item);
            return FL_RESULT_BUSY;
        }
    } else {
        if (wq->st.timer_count + wq->st.periodic_inflight >= FL_WQ_TIMERS_MAX) {
            WQ_UNLOCK(wq);
            free(item);
            return FL_RESULT_BUSY;
        }
        wq_timer_insert(wq, item);
    }
    WQ_UNLOCK(wq);
    return FL_RESULT_OK;
}

fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work) {
    if (!work)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, work->run_at_ns, work->period_ns);
}

fl_result_t fl_wq_enqueue_at(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns) {
    if (!work || run_at_ns <= 0)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, run_at_ns, work->period_ns);
}

fl_result_t fl_wq_enqueue_every(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t period_ns) {
    if (!work || period_ns <= 0)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, 0, period_ns);
}

/* Move every due timer into the MLQ, earliest first. Caller holds the lock. */
static void wq_promote_due(fl_workqueue_t *wq, int64_t now_ns) {
    while (wq->st.timer_count > 0) {
        wq_item_t *item = wq_timer_at(wq, 0);

        if (item->run_at_ns > now_ns)
            break;
        if (pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0)
            break; /* MLQ full: stays due, tried again next poll */
        wq_timer_remove_head(wq);
        if (item->period_ns > 0)
            wq->st.periodic_inflight++;
    }
}

unsigned fl_wq_poll_at(fl_workqueue_t *wq, unsigned max_items, int64_t now_ns) {
    unsigned ran = 0;

    if (!wq || max_items == 0)
        return 0;

    WQ_LOCK(wq);
#ifdef FL_WQ_DEBUG_REENTRANCY
    assert(wq->st.poll_depth == 0 &&
           "fl_wq_poll: one poller per queue — no nested poll before P9-3 SMP");
#endif
    wq->st.poll_depth++;
    wq->st.poll_now_ns = now_ns;
    if (now_ns >= 0)
        wq_promote_due(wq, now_ns);

    while (ran < max_items && !pq_is_empty(&wq->st.pq)) {
        pq_task_t task;

        if (pq_pop(&wq->st.pq, &task) != 0)
            break;
        WQ_UNLOCK(wq);
        if (task.fn)
            task.fn(task.arg);
        WQ_LOCK(wq);
        ran++;
    }

    wq->st.poll_depth--;
    WQ_UNLOCK(wq);
    return ran;
}

unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items) {
    int64_t now_ns = -1;

    if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
        now_ns = -1;
    return fl_wq_poll_at(wq, max_items, now_ns);
}

static int wq_mlq_empty(fl_workqueue_t *wq) {
    int empty;

    WQ_LOCK(wq);
    empty = pq_is_empty(&wq->st.pq);
    WQ_UNLOCK(wq);
    return empty;
}

fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms) {
    int64_t deadline_ns = 0;
    int have_deadline = 0;

    if (!wq)
        return FL_RESULT_INVAL;

    if (timeout_ms > 0) {
        int64_t start_ns = 0;

        if (fl_time_monotonic_ns(&start_ns) == FL_RESULT_OK) {
            deadline_ns = start_ns + (int64_t)timeout_ms * 1000000LL;
            have_deadline = 1;
        }
        /* else: no P1-7 on this path — fall through to best-effort single poll */
    }

    for (;;) {
        if (wq_mlq_empty(wq))
            return FL_RESULT_OK;

        (void)fl_wq_poll(wq, FL_WQ_PENDING_MAX);

        if (timeout_ms == 0)
            continue;

        if (!have_deadline) {
            if (wq_mlq_empty(wq))
                return FL_RESULT_OK;
            return FL_RESULT_BUSY;
        }

        {
            int64_t now_ns = 0;

            if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
                return FL_RESULT_BUSY;
            if (now_ns >= deadline_ns)
                return FL_RESULT_BUSY;
        }
    }
}
