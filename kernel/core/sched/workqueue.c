#include "workqueue.h"

#include <stdlib.h>
#include <string.h>

typedef struct wq_node {
    fl_wq_work_t work;
    struct wq_node *next;
} wq_node_t;

struct fl_workqueue {
    wq_node_t *head;
    wq_node_t *tail;
    unsigned count;
    unsigned shutdown;
};

static fl_workqueue_t s_default_wq;
static int s_default_inited;

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
    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
    wq->shutdown = 0;
    return FL_RESULT_OK;
}

void fl_wq_shutdown(fl_workqueue_t *wq) {
    wq_node_t *n;
    wq_node_t *next;

    if (!wq)
        return;
    wq->shutdown = 1;
    for (n = wq->head; n; n = next) {
        next = n->next;
        /* Hosted builds may use libc; **B** path should use arena allocator later. */
        free(n);
    }
    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
}

fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work) {
    wq_node_t *n;

    if (!wq || !work || !work->fn)
        return FL_RESULT_INVAL;
    if (wq->shutdown)
        return FL_RESULT_BUSY;
    if (wq->count >= FL_WQ_PENDING_MAX)
        return FL_RESULT_BUSY;

    n = (wq_node_t *)calloc(1, sizeof(*n));
    if (!n)
        return FL_RESULT_NOMEM;

    n->work = *work;
    n->next = NULL;
    if (wq->tail)
        wq->tail->next = n;
    else
        wq->head = n;
    wq->tail = n;
    wq->count++;
    return FL_RESULT_OK;
}

unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items) {
    unsigned ran = 0;

    if (!wq || max_items == 0)
        return 0;

    while (ran < max_items && wq->head) {
        wq_node_t *n = wq->head;
        fl_wq_work_t work = n->work;

        wq->head = n->next;
        if (!wq->head)
            wq->tail = NULL;
        wq->count--;
        free(n);

        if (work.fn)
            work.fn(work.ctx);
        ran++;
    }
    return ran;
}

fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms) {
    (void)timeout_ms;
    if (!wq)
        return FL_RESULT_INVAL;
    while (wq->head)
        (void)fl_wq_poll(wq, FL_WQ_PENDING_MAX);
    return FL_RESULT_OK;
}
