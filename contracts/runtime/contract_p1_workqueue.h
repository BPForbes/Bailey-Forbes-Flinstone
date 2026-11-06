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
#define FL_CONTRACT_P1_WORKQUEUE_REV 5

/** Maximum pending work items per queue (lab default). */
#define FL_WQ_PENDING_MAX 64u

/**
 * Slots of **FL_WQ_PENDING_MAX** that only **FL_WQ_PROTECTED** items may use:
 * ordinary work stops this many short of the cap (FL-22). Design value.
 */
#define FL_WQ_PROTECTED_RESERVE 8u

/** Delayed / periodic items waiting for their due time, per queue (FL-22). Design value. */
#define FL_WQ_TIMERS_MAX 32u

/**
 * Durable work (FL-25). Design values: nothing in the repo sets one. A record's payload is at most
 * FL_WQ_PAYLOAD_MAX bytes; the journal refuses a new key (FL_RESULT_BUSY) when FL_WQ_JOURNAL_MAX_PENDING
 * records are outstanding (PENDING, QUEUED, RUNNING or HELD; a repeated key is answered first); a durable
 * job is tried FL_WQ_MAX_ATTEMPTS times; FL_WQ_HANDLERS_MAX tags and FL_WQ_EXECUTORS_MAX executors can be
 * registered. At most FL_WQ_PENDING_MAX / 2 durable jobs sit in the in-memory window at once.
 * Rev 5: staging reserves its slot before claiming, so reservations count in every capacity check, and a
 * record belongs to one executor id. FL_WQ_EXECUTOR_* and FL_WQ_ACCEPTED_UNSTAGED are defined in
 * kernel/core/sched/workqueue.h with the record type they belong to.
 */
#define FL_WQ_PAYLOAD_MAX 256u
#define FL_WQ_JOURNAL_MAX_PENDING 1024u
#define FL_WQ_MAX_ATTEMPTS 3u
#define FL_WQ_HANDLERS_MAX 16u
/** Durable executors that can be registered (rev 5). */
#define FL_WQ_EXECUTORS_MAX 8u

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
