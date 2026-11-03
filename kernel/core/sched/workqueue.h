#ifndef FL_WORKQUEUE_H
#define FL_WORKQUEUE_H

#include "contract_p1_workqueue.h"
#include "contract_result.h"
#include "priority_queue.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Two builds share this source. Hosted: each queue carries a mutex. Freestanding (the browser
 * kernel image, built with -DFL_FREESTANDING: no libc, no threads, one CPU whose interrupts only set
 * flags): no lock and a static item pool instead of calloc. The layout of fl_workqueue_storage_t
 * follows that one define, never __STDC_HOSTED__, so a compile that gets it wrong is refused here.
 */
#ifndef FL_FREESTANDING
#if defined(__STDC_HOSTED__) && !__STDC_HOSTED__
#error "a -ffreestanding build must define FL_FREESTANDING (it selects the unlocked, pool-backed layout)"
#endif
#define FL_WQ_HAS_LOCK 1
#include <pthread.h>
#elif defined(__STDC_HOSTED__) && __STDC_HOSTED__ && !defined(FL_WQ_ALLOW_HOSTED_FREESTANDING)
#error "FL_FREESTANDING without -ffreestanding: define FL_WQ_ALLOW_HOSTED_FREESTANDING for a host test of that configuration"
#endif

typedef struct fl_workqueue fl_workqueue_t;

/** Stack/static footprint for **fl_wq_*** APIs (tests and embed). */
typedef struct fl_workqueue_storage {
    priority_queue_t pq;
    unsigned shutdown;
    unsigned poll_depth;                  /* debug nested-poll guard, per queue */
    unsigned timer_count;                 /* delayed items, ordered by due time */
    unsigned periodic_inflight;           /* periodic items promoted or running: keep a timer slot */
    int64_t poll_now_ns;                  /* time of the poll in progress; -1 = none or no clock */
    void *timers[FL_WQ_TIMERS_MAX];
#ifdef FL_WQ_HAS_LOCK
    pthread_mutex_t lock;
#endif
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
    /* Delayed and periodic work (P1-7 monotonic time; zero-initialised = as before). */
    int64_t run_at_ns;  /* 0 = run as soon as polled; else not before this fl_time_monotonic_ns value */
    int64_t period_ns;  /* 0 = one shot; > 0 = re-armed period_ns after each run */
    uint32_t flags;     /* FL_WQ_* flags */
} fl_wq_work_t;

/** Never discarded by **fl_wq_shutdown** (runs once first); may use the reserve slots. */
#define FL_WQ_PROTECTED 0x1u

/** Global default workqueue for kernel background jobs (**P1-8**). Single-writer; see contract shard. */
fl_workqueue_t *fl_wq_default(void);

fl_result_t fl_wq_init(fl_workqueue_t *wq);
void fl_wq_shutdown(fl_workqueue_t *wq);

/** Pending item count (for tests and watchdog). */
int fl_wq_pending(const fl_workqueue_t *wq);

/**
 * Enqueue work; returns **FL_RESULT_BUSY** when full. With **run_at_ns** and **period_ns**
 * both zero it goes straight to the MLQ (one shot). Otherwise it waits for its due time
 * (**period_ns > 0** with no **run_at_ns** is due one period from now). Ordinary items stop
 * **FL_WQ_PROTECTED_RESERVE** short of **FL_WQ_PENDING_MAX**; **FL_WQ_PROTECTED** items use it all.
 */
fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work);

/** Run once at or after **run_at_ns** (**fl_time_monotonic_ns** scale). Same as enqueue with that field set. */
fl_result_t fl_wq_enqueue_at(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns);

/** Run every **period_ns** (> 0), first one period from now, then re-armed after each run. */
fl_result_t fl_wq_enqueue_every(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t period_ns);

/**
 * Move due timers into the MLQ, then pop and run up to **max_items** tasks (highest-priority
 * layer first). Reads **fl_time_monotonic_ns**; with no clock only items without a due time run.
 */
unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items);

/** **fl_wq_poll** with the time supplied by the caller (tests, tick-driven freestanding). */
unsigned fl_wq_poll_at(fl_workqueue_t *wq, unsigned max_items, int64_t now_ns);

/**
 * Drain work already in the MLQ (delayed and periodic items still waiting for their due time
 * are not waited for). **timeout_ms == 0** blocks until empty (hosted poll loop).
 * **timeout_ms > 0** uses **P1-7** monotonic time; returns **FL_RESULT_BUSY** on timeout.
 */
fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms);

#endif /* FL_WORKQUEUE_H */
