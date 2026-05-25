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

#endif /* FL_CONTRACT_P1_WORKQUEUE_H */
