/*
 * P1-8 — Kernel background jobs / workqueues (contract shard).
 *
 * Normative vocabulary for deferred work outside hardirq and outside the
 * interactive shell path. Implementation: kernel/core/sched/workqueue.c on
 * priority_queue_t (same MLQ as threadpool).
 */
#ifndef FL_CONTRACT_P1_WORKQUEUE_H
#define FL_CONTRACT_P1_WORKQUEUE_H

#include "contract_extend.h"

#define FL_CONTRACT_P1_WORKQUEUE_CONTRACT_DEFINED 1
#define FL_CONTRACT_P1_WORKQUEUE_REV 2

/** Maximum pending work items per queue (lab default). */
#define FL_WQ_PENDING_MAX 64u

/**
 * Slots of **FL_WQ_PENDING_MAX** that only **FL_WQ_PROTECTED** items may use:
 * ordinary work stops this many short of the cap (FL-22). Design value.
 */
#define FL_WQ_PROTECTED_RESERVE 8u

/** Delayed / periodic items waiting for their due time, per queue (FL-22). Design value. */
#define FL_WQ_TIMERS_MAX 32u

/** Work handler must not block indefinitely; may not allocate without bound. */
#define FL_WQ_HANDLER_STACK_HINT_BYTES 512u

/**
 * MLQ layer indices (0 = highest priority). Same semantics as
 * **PQ_NUM_PRIORITIES** in **priority_queue.h** / shell **threadpool**.
 */
#define FL_WQ_LAYER_URGENT      0 /* watchdog, time-critical */
#define FL_WQ_LAYER_NORMAL      1
#define FL_WQ_LAYER_BACKGROUND  2 /* reclaim, net timers */
#define FL_WQ_LAYER_IDLE        3

/**
 * **fl_wq_default()** has one poller: one thread (or tick loop) calls **fl_wq_poll**.
 * Hosted builds guard enqueue, poll bookkeeping and the timer array with one
 * mutex per queue (initialised by **fl_wq_init**, never held while a handler runs), so
 * shell, service and ticker threads may all enqueue. Freestanding builds have no
 * threads and take no lock. Debug builds assert no nested **fl_wq_poll** on the
 * same queue (**FL_WQ_DEBUG_REENTRANCY**); two queues may be polled by two threads.
 *
 * Delayed and periodic work (**run_at_ns**, **period_ns**, P1-7 monotonic time)
 * waits in a small time-ordered array beside the MLQ and is moved into its
 * layer when due. **FL_WQ_PROTECTED** work is never discarded by
 * **fl_wq_shutdown** (it runs once first) and may use the reserve slots.
 */

#endif /* FL_CONTRACT_P1_WORKQUEUE_H */
