#ifndef FL_WORKQUEUE_H
#define FL_WORKQUEUE_H

#include "contract_p1_workqueue.h"
#include "contract_result.h"

#include <stddef.h>
#include <stdint.h>

typedef struct fl_workqueue fl_workqueue_t;

typedef void (*fl_wq_work_fn_t)(void *ctx);

typedef struct {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag; /* optional domain id (P1-9, P3-14, …) */
} fl_wq_work_t;

/** Global default workqueue for kernel background jobs (**P1-8**). */
fl_workqueue_t *fl_wq_default(void);

fl_result_t fl_wq_init(fl_workqueue_t *wq);
void fl_wq_shutdown(fl_workqueue_t *wq);

/** Enqueue one shot of work; returns **FL_RESULT_BUSY** when **FL_WQ_PENDING_MAX** reached. */
fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work);

/** Run up to **max_items** pending handlers (hosted poll / tick entry). */
unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items);

/** Block until pending queue is empty or **timeout_ms** elapses (hosted **H** only). */
fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms);

#endif /* FL_WORKQUEUE_H */
