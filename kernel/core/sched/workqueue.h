#ifndef FL_WORKQUEUE_H
#define FL_WORKQUEUE_H

#include "contract_p1_workqueue.h"
#include "contract_p2_heritage.h"
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
    unsigned durable_inmem;               /* durable jobs staged in the MLQ (the window over the journal) */
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
    uint32_t tag; /* optional domain id  */
    /**
     * **FL_WQ_LAYER_*** index (0 = highest). Values outside **0..PQ_NUM_PRIORITIES-1**
     * use **FL_WQ_LAYER_BACKGROUND**.
     */
    int pq_layer;
    int64_t run_at_ns;  /* 0 = run as soon as polled; else not before this fl_time_monotonic_ns value */
    int64_t period_ns;  /* 0 = one shot; > 0 = re-armed period_ns after each run */
    uint32_t flags;     /* FL_WQ_* flags */
} fl_wq_work_t;

/** Never discarded by **fl_wq_shutdown** (runs once first); may use the reserve slots. */
#define FL_WQ_PROTECTED 0x1u

/** Global default workqueue for kernel background jobs. Single-writer; see contract shard. */
fl_workqueue_t *fl_wq_default(void);

/** Initialises storage that was never initialised, or that **fl_wq_destroy** ended. Never call it on a live queue. */
fl_result_t fl_wq_init(fl_workqueue_t *wq);
void fl_wq_shutdown(fl_workqueue_t *wq);
/**
 * Ends a queue's life: destroys its mutex. Only after **fl_wq_shutdown**, and only when no thread can
 * still enqueue, poll or resolve on it. The storage may then be passed to **fl_wq_init** again.
 * **fl_wq_default()** is never destroyed.
 */
void fl_wq_destroy(fl_workqueue_t *wq);

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
 * **timeout_ms > 0** uses monotonic time; returns **FL_RESULT_BUSY** on timeout.
 */
fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms);


/* Durable work: journal first, recover at boot, bounded backlog.
 * A durable job is a tag (a handler registered at every boot: function pointers are never persisted),
 * a payload and an idempotency key. It is appended to a journal BEFORE it is accepted; the in-memory
 * MLQ holds only a window of it. The journal is an installable table of operations (the shape of
 * fl_kq_storage_t): the default is memory only (lost on reboot, the only choice in the browser image),
 * kernel/core/sched/wq_journal_sqlite.c is the hosted, flushed one. */

typedef enum {
    FL_WQ_ST_PENDING = 1,
    FL_WQ_ST_RUNNING,
    FL_WQ_ST_DONE,
    FL_WQ_ST_FAILED,
    FL_WQ_ST_EXPIRED,
    FL_WQ_ST_HELD,    /* recovered, waiting for the user's decision */
    FL_WQ_ST_QUEUED,  /* staged in the in-memory window (appended: states are never renumbered) */
    FL_WQ_ST_SKIPPED  /* the user declined a held occurrence */
} fl_wq_state_t;

/** Per tag: may a recovered job wait for the user? */
#define FL_WQ_RECOVER_AUTO 0 /* runs at recovery: monitor obligations are never optional */
#define FL_WQ_RECOVER_ASK 1  /* user-visible work such as cron: held until the user decides */

typedef struct fl_wq_journal_record {
    uint64_t id;                         /* assigned by the journal: arrival order */
    uint8_t idem_key[16];                /* UNIQUE: a repeat returns the existing record */
    uint32_t tag;
    int layer;                           /* FL_WQ_LAYER_* */
    uint8_t recover;                     /* FL_WQ_RECOVER_AUTO or _ASK (copied from the tag at append) */
    char owner[FL_HERITAGE_KEY_ID_MAX];  /* heritage key id: who is asked about a held job; "" = none */
    fl_wq_state_t state;
    uint32_t attempts, max_attempts;     /* attempts counts every move to RUNNING */
    int64_t accepted_at, deadline;       /* seconds; deadline 0 = none */
    uint32_t payload_len;
    uint8_t payload[FL_WQ_PAYLOAD_MAX];
} fl_wq_journal_record_t;

/**
 * Operations a journal backend provides. Each is atomic on its own. append returns only once the
 * record is durable (for a flushed backend) and, for a repeated idem_key, the existing id with
 * *existing = 1 and nothing written. mark is a compare-and-set: it moves a job from `from` to `to`
 * and returns FL_RESULT_BUSY if the job is no longer in `from` (a lost race is harmless); a move to
 * RUNNING counts one attempt. scan returns up to max records in `state` (any owner when owner is
 * NULL) ordered by (layer, id) strictly after the cursor record (NULL = from the start).
 */
typedef struct fl_wq_journal_ops {
    void *ctx;
    fl_result_t (*append)(void *ctx, const fl_wq_journal_record_t *r, uint64_t *id_out, int *existing);
    fl_result_t (*mark)(void *ctx, uint64_t id, fl_wq_state_t from, fl_wq_state_t to);
    fl_result_t (*load)(void *ctx, uint64_t id, fl_wq_journal_record_t *out);
    fl_result_t (*scan)(void *ctx, fl_wq_state_t state, const char *owner, const fl_wq_journal_record_t *after,
                        unsigned max, fl_wq_journal_record_t *out, unsigned *n);
    unsigned (*count)(void *ctx, fl_wq_state_t state, const char *owner);
} fl_wq_journal_ops_t;

const fl_wq_journal_ops_t *fl_wq_journal_mem(void);
void fl_wq_journal_mem_reset(void);
/** Install a journal (NULL restores the default); returns the one in force before. Set once at start,
 *  not while durable work is queued or running. */
const fl_wq_journal_ops_t *fl_wq_set_journal(const fl_wq_journal_ops_t *ops);

/** A handler runs on the poll thread and must be idempotent on idem_key: it may run twice. FL_RESULT_OK
 *  is done; anything else is a failed attempt (retried up to FL_WQ_MAX_ATTEMPTS). */
typedef fl_result_t (*fl_wq_handler_fn_t)(const void *payload, size_t n, const uint8_t idem_key[16]);

/** tag -> function (and its recovery rule), rebuilt at every boot before fl_wq_recover. A tag
 *  registered twice is refused (BUSY). Single-thread, at start. */
fl_result_t fl_wq_register_handler(uint32_t tag, fl_wq_handler_fn_t fn, uint8_t recover);

/**
 * Accept a durable job: persisted FIRST, then staged. FL_RESULT_BUSY when the journal is full
 * (FL_WQ_JOURNAL_MAX_PENDING waiting): a clear "retry later". A repeated idem_key is OK and queues
 * nothing new. ttl_sec <= 0 means no deadline. owner may be NULL.
 */
fl_result_t fl_wq_enqueue_durable(fl_workqueue_t *wq, uint32_t tag, int layer, const char *owner,
                                  const void *payload, size_t n, const uint8_t idem_key[16],
                                  int64_t ttl_sec);

/** The durable job state machine, from the Lean 4 model (formal/lean): 1 when the queue makes the move
 *  `from` -> `to` (fl_wq_state_t numbers), 0 for any other pair and for a number that is no state. */
unsigned fl_wq_model_transition_allowed(unsigned from, unsigned to);
/** 1 for DONE, FAILED, EXPIRED and SKIPPED. */
unsigned fl_wq_model_state_terminal(unsigned state);
/** State changes the queue itself asked a journal for that the model forbids. They are refused, and this
 *  counter must stay 0. */
unsigned fl_wq_illegal_moves(void);

/** Once at boot, before the first poll: cut-off work is retried (or FAILED past its attempts), stale
 *  work becomes EXPIRED (never run silently), RECOVER_ASK work is HELD, the rest is staged. */
fl_result_t fl_wq_recover(fl_workqueue_t *wq);
unsigned fl_wq_held_count(const char *owner);
/** run_now: held jobs of this owner are queued; otherwise only this occurrence is dropped (SKIPPED). */
fl_result_t fl_wq_resolve_held(const char *owner, int run_now);

#endif /* FL_WORKQUEUE_H */
