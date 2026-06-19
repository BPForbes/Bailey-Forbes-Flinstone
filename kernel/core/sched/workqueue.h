#ifndef FL_WORKQUEUE_H
#define FL_WORKQUEUE_H

#include "contract_p1_workqueue.h"
#include "contract_result.h"
#include "priority_queue.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fl_workqueue fl_workqueue_t;

/** Stack/static footprint for **fl_wq_*** APIs (tests and embed). */
typedef struct fl_workqueue_storage {
    priority_queue_t pq;
    unsigned shutdown;
} fl_workqueue_storage_t;

typedef void (*fl_wq_work_fn_t)(void *ctx);

typedef struct {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag; /* optional domain id (P1-9, P3-14, …) */
    /**
     * **FL_WQ_LAYER_*** index (0 = highest). Values outside **0..PQ_NUM_PRIORITIES-1**
     * use **FL_WQ_LAYER_BACKGROUND**.
     */
    int pq_layer;
} fl_wq_work_t;

/** Global default workqueue for kernel background jobs (**P1-8**). Single-writer; see contract shard. */
fl_workqueue_t *fl_wq_default(void);

fl_result_t fl_wq_init(fl_workqueue_t *wq);
void fl_wq_shutdown(fl_workqueue_t *wq);

/** Pending item count (for tests and watchdog). */
int fl_wq_pending(const fl_workqueue_t *wq);

/** Enqueue one shot of work on the MLQ; returns **FL_RESULT_BUSY** when full. */
fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work);

/** Pop and run up to **max_items** tasks (highest-priority layer first). */
unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items);

/**
 * Drain pending work. **timeout_ms == 0** blocks until empty (hosted poll loop).
 * **timeout_ms > 0** uses **P1-7** monotonic time; returns **FL_RESULT_BUSY** on timeout.
 */
fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms);

#endif /* FL_WORKQUEUE_H */
