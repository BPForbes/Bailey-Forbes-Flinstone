#ifndef FL_WORKQUEUE_H
#define FL_WORKQUEUE_H

#include "contract_p1_workqueue.h"
#include "contract_result.h"
#include "priority_queue.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fl_workqueue fl_workqueue_t;

typedef void (*fl_wq_work_fn_t)(void *ctx);

/** MLQ layer for kernel deferred work (same **priority_queue_t** as shell **threadpool**). */
#define FL_WQ_LAYER_URGENT      0 /* watchdog, time-critical */
#define FL_WQ_LAYER_NORMAL      1
#define FL_WQ_LAYER_BACKGROUND  2 /* reclaim, net timers */
#define FL_WQ_LAYER_IDLE        3

typedef struct {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag; /* optional domain id (P1-9, P3-14, …) */
    /**
     * **PQ_NUM_PRIORITIES** layer (0 = highest). Values outside range use
     * **FL_WQ_LAYER_BACKGROUND**.
     */
    int pq_layer;
} fl_wq_work_t;

/** Global default workqueue for kernel background jobs (**P1-8**). */
fl_workqueue_t *fl_wq_default(void);

fl_result_t fl_wq_init(fl_workqueue_t *wq);
void fl_wq_shutdown(fl_workqueue_t *wq);

/** Enqueue one shot of work on the MLQ; returns **FL_RESULT_BUSY** when full. */
fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work);

/** Pop and run up to **max_items** tasks (highest-priority layer first). */
unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items);

/** Block until pending queue is empty or **timeout_ms** elapses (hosted **H** only). */
fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms);

#endif /* FL_WORKQUEUE_H */
