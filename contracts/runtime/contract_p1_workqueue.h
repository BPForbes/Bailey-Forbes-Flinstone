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
#define FL_CONTRACT_P1_WORKQUEUE_REV 1

/** Maximum pending work items per queue (lab default). */
#define FL_WQ_PENDING_MAX 64u

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
 * **fl_wq_default()** is single-writer until **P9-3** SMP: one thread (or
 * tick loop) may call **fl_wq_enqueue** / **fl_wq_poll** without locks.
 * Debug builds may assert no nested **fl_wq_poll** (**FL_WQ_DEBUG_REENTRANCY**).
 */

#endif /* FL_CONTRACT_P1_WORKQUEUE_H */
