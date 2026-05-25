#include "workqueue.h"

#include "timekeeping.h"

#include <assert.h>
#include <stdlib.h>

#ifndef NDEBUG
#define FL_WQ_DEBUG_REENTRANCY 1
#endif

typedef struct {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag;
} wq_item_t;

struct fl_workqueue {
    priority_queue_t pq;
    unsigned shutdown;
};

_Static_assert(sizeof(struct fl_workqueue) == sizeof(fl_workqueue_storage_t),
               "fl_workqueue_storage_t must match fl_workqueue_t layout");

static fl_workqueue_t s_default_wq;
static int s_default_inited;

#ifdef FL_WQ_DEBUG_REENTRANCY
static unsigned s_wq_poll_depth;
#endif

static void wq_dispatch(void *arg) {
    wq_item_t *item = (wq_item_t *)arg;

    if (item) {
        if (item->fn)
            item->fn(item->ctx);
        free(item);
    }
}

static int wq_resolve_layer(const fl_wq_work_t *work) {
    int layer = FL_WQ_LAYER_BACKGROUND;

    if (!work)
        return layer;
    if (work->pq_layer >= 0 && work->pq_layer < PQ_NUM_PRIORITIES)
        layer = work->pq_layer;
    return layer;
}

static void wq_discard_task(pq_task_t *task) {
    if (task && task->arg)
        free(task->arg);
}

fl_workqueue_t *fl_wq_default(void) {
    if (!s_default_inited) {
        (void)fl_wq_init(&s_default_wq);
        s_default_inited = 1;
    }
    return &s_default_wq;
}

fl_result_t fl_wq_init(fl_workqueue_t *wq) {
    if (!wq)
        return FL_RESULT_INVAL;
    pq_init(&wq->pq);
    wq->shutdown = 0;
    return FL_RESULT_OK;
}

void fl_wq_shutdown(fl_workqueue_t *wq) {
    pq_task_t task;

    if (!wq)
        return;
    wq->shutdown = 1;
    while (!pq_is_empty(&wq->pq)) {
        if (pq_pop(&wq->pq, &task) != 0)
            break;
        wq_discard_task(&task);
    }
}

int fl_wq_pending(const fl_workqueue_t *wq) {
    return wq ? pq_count(&wq->pq) : 0;
}

fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work) {
    wq_item_t *item;
    int layer;
    pq_handle_t h;

    if (!wq || !work || !work->fn)
        return FL_RESULT_INVAL;
    if (wq->shutdown)
        return FL_RESULT_BUSY;
    if ((unsigned)pq_count(&wq->pq) >= FL_WQ_PENDING_MAX)
        return FL_RESULT_BUSY;

    item = (wq_item_t *)calloc(1, sizeof(*item));
    if (!item)
        return FL_RESULT_NOMEM;

    item->fn = work->fn;
    item->ctx = work->ctx;
    item->tag = work->tag;

    layer = wq_resolve_layer(work);
    h = pq_push(&wq->pq, layer, wq_dispatch, item);
    if (h < 0) {
        free(item);
        return FL_RESULT_BUSY;
    }
    return FL_RESULT_OK;
}

unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items) {
    unsigned ran = 0;

    if (!wq || max_items == 0)
        return 0;

#ifdef FL_WQ_DEBUG_REENTRANCY
    assert(s_wq_poll_depth == 0 &&
           "fl_wq_poll: single-writer contract — no nested poll before P9-3 SMP");
    s_wq_poll_depth++;
#endif

    while (ran < max_items && !pq_is_empty(&wq->pq)) {
        pq_task_t task;

        if (pq_pop(&wq->pq, &task) != 0)
            break;
        if (task.fn)
            task.fn(task.arg);
        ran++;
    }

#ifdef FL_WQ_DEBUG_REENTRANCY
    s_wq_poll_depth--;
#endif
    return ran;
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
        if (pq_is_empty(&wq->pq))
            return FL_RESULT_OK;

        (void)fl_wq_poll(wq, FL_WQ_PENDING_MAX);

        if (timeout_ms == 0)
            continue;

        if (!have_deadline)
            return FL_RESULT_BUSY;

        {
            int64_t now_ns = 0;

            if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
                return FL_RESULT_BUSY;
            if (now_ns >= deadline_ns)
                return FL_RESULT_BUSY;
        }
    }
}
