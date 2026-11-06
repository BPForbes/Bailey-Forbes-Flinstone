#include "workqueue.h"

#include "timekeeping.h"

#ifndef FL_FREESTANDING
#include <assert.h>
#include <sched.h>
#include <stdlib.h>
#endif

#if !defined(NDEBUG) && !defined(FL_FREESTANDING)
#define FL_WQ_DEBUG_REENTRANCY 1
#endif

/* The durable job state machine's transition table and terminal states, generated from the Lean 4 model
 * (formal/lean/FlWqCode.lean; FlWq.lean proves they are the model's). No Lean runtime is involved. */
#include "wq_model_gen.inc"

static unsigned s_illegal_moves;

unsigned fl_wq_model_transition_allowed(unsigned from, unsigned to) {
    return from <= 255u && to <= 255u && fl_wq_transition_allowed((uint8_t)from, (uint8_t)to);
}

unsigned fl_wq_model_state_terminal(unsigned state) {
    return state <= 255u && fl_wq_state_terminal((uint8_t)state);
}

unsigned fl_wq_illegal_moves(void) {
    return __atomic_load_n(&s_illegal_moves, __ATOMIC_RELAXED);
}

typedef struct wq_item {
    fl_wq_work_fn_t fn;
    void *ctx;
    uint32_t tag;
    int layer;
    int64_t run_at_ns;
    int64_t period_ns;
    uint32_t flags;
    uint64_t job_id; /* nonzero: a durable job staged from the journal */
    struct fl_workqueue *owner;
} wq_item_t;

/* Item storage. Hosted: one calloc per item. Freestanding has no heap: a static pool sized for
 * everything that can be pending at once (MLQ cap + timers), used from the one polling context
 * and from enqueuers that run with interrupts' work deferred, so it needs no lock. */
#ifdef FL_FREESTANDING
#define WQ_POOL_ITEMS (FL_WQ_PENDING_MAX + FL_WQ_TIMERS_MAX)
static wq_item_t s_item_pool[WQ_POOL_ITEMS];
static unsigned char s_item_used[WQ_POOL_ITEMS];

static wq_item_t *wq_item_alloc(void) {
    for (unsigned i = 0; i < WQ_POOL_ITEMS; i++) {
        if (!s_item_used[i]) {
            unsigned char *b = (unsigned char *)&s_item_pool[i];

            s_item_used[i] = 1;
            for (unsigned k = 0; k < sizeof(wq_item_t); k++) /* no memset: the image has no libc */
                b[k] = 0;
            return &s_item_pool[i];
        }
    }
    return NULL;
}

static void wq_item_free(wq_item_t *item) {
    s_item_used[item - s_item_pool] = 0;
}
#else
static wq_item_t *wq_item_alloc(void) {
    return (wq_item_t *)calloc(1, sizeof(wq_item_t));
}

static void wq_item_free(wq_item_t *item) {
    free(item);
}
#endif

/* The queue is its storage type, so the two can never drift apart. */
struct fl_workqueue {
    fl_workqueue_storage_t st;
};

/* One lock for the executor registry and the completion retry table: both are tiny and touched off the
 * hot path. Freestanding has one context and takes none. */
#ifdef FL_WQ_HAS_LOCK
static pthread_mutex_t s_wq_global_mu = PTHREAD_MUTEX_INITIALIZER;
#define WQ_GLOBAL_LOCK() (void)pthread_mutex_lock(&s_wq_global_mu)
#define WQ_GLOBAL_UNLOCK() (void)pthread_mutex_unlock(&s_wq_global_mu)
#else
#define WQ_GLOBAL_LOCK() ((void)0)
#define WQ_GLOBAL_UNLOCK() ((void)0)
#endif

static struct {
    uint32_t id;
    fl_workqueue_t *wq;
} s_executors[FL_WQ_EXECUTORS_MAX];
static unsigned s_executor_count;

/* Caller holds the global lock. */
static void wq_unbind_locked(const fl_workqueue_t *wq) {
    unsigned i = 0;

    while (i < s_executor_count) {
        if (s_executors[i].wq == wq)
            s_executors[i] = s_executors[--s_executor_count];
        else
            i++;
    }
}

static void wq_unregister_executor(const fl_workqueue_t *wq) {
    WQ_GLOBAL_LOCK();
    wq_unbind_locked(wq);
    WQ_GLOBAL_UNLOCK();
}

static uint32_t wq_executor_of(const fl_workqueue_t *wq) {
    return __atomic_load_n(&wq->st.executor, __ATOMIC_RELAXED);
}

/* Startup only (see the header). The field is read and written atomically, so a misuse is a stale read,
 * never a torn one. A refused call changes nothing. */
fl_result_t fl_wq_set_executor(fl_workqueue_t *wq, uint32_t executor) {
    unsigned i = 0, others = 0;

    if (!wq || executor == FL_WQ_EXECUTOR_NONE || executor == FL_WQ_EXECUTOR_ANY)
        return FL_RESULT_INVAL;
    WQ_GLOBAL_LOCK();
    for (unsigned k = 0; k < s_executor_count; k++)
        others += s_executors[k].wq != wq && s_executors[k].id != executor;
    if (others >= FL_WQ_EXECUTORS_MAX) {
        WQ_GLOBAL_UNLOCK();
        return FL_RESULT_NOMEM;
    }
    wq_unbind_locked(wq); /* one queue, one executor id */
    while (i < s_executor_count && s_executors[i].id != executor)
        i++;
    if (i == s_executor_count)
        s_executor_count++;
    s_executors[i].id = executor;
    s_executors[i].wq = wq;
    __atomic_store_n(&wq->st.executor, executor, __ATOMIC_RELAXED);
    WQ_GLOBAL_UNLOCK();
    return FL_RESULT_OK;
}

static int wq_executor_known(uint32_t executor) {
    int known = 0;

    WQ_GLOBAL_LOCK();
    for (unsigned i = 0; i < s_executor_count; i++)
        known |= s_executors[i].id == executor;
    WQ_GLOBAL_UNLOCK();
    return known;
}

static fl_workqueue_t s_default_wq;
#ifdef FL_WQ_HAS_LOCK
static pthread_once_t s_default_once = PTHREAD_ONCE_INIT;
#else
static int s_default_inited;
#endif

#ifdef FL_WQ_HAS_LOCK
#define WQ_LOCK(wq) (void)pthread_mutex_lock(&(wq)->st.lock)
#define WQ_UNLOCK(wq) (void)pthread_mutex_unlock(&(wq)->st.lock)
#else
#define WQ_LOCK(wq) ((void)0)
#define WQ_UNLOCK(wq) ((void)0)
#endif

static wq_item_t *wq_timer_at(const fl_workqueue_t *wq, unsigned i) {
    return (wq_item_t *)wq->st.timers[i];
}

/* Keep timers ordered by due time; equal times keep arrival order. Caller holds the lock
 * and has checked there is room. */
static void wq_timer_insert(fl_workqueue_t *wq, wq_item_t *item) {
    unsigned i = wq->st.timer_count;

    while (i > 0 && wq_timer_at(wq, i - 1)->run_at_ns > item->run_at_ns) {
        wq->st.timers[i] = wq->st.timers[i - 1];
        i--;
    }
    wq->st.timers[i] = item;
    wq->st.timer_count++;
}

static void wq_timer_remove_head(fl_workqueue_t *wq) {
    for (unsigned i = 1; i < wq->st.timer_count; i++)
        wq->st.timers[i - 1] = wq->st.timers[i];
    wq->st.timer_count--;
    wq->st.timers[wq->st.timer_count] = NULL;
}

static void wq_run_durable(wq_item_t *item);
static fl_result_t wq_fill_window(fl_workqueue_t *wq);

/* Caller holds the lock. */
static int64_t wq_next_periodic_run(const fl_workqueue_t *wq, const wq_item_t *item) {
    int64_t base = wq->st.poll_now_ns >= 0 ? wq->st.poll_now_ns : item->run_at_ns;
    int64_t next;

    /* a huge period must not wrap negative (that would run the item on every poll) */
    if (__builtin_add_overflow(base, item->period_ns, &next))
        next = INT64_MAX;
    return next;
}

/* Runs on the poll thread, with the lock released. A periodic item goes back to the timer
 * array (its slot was kept in periodic_inflight, so this cannot fail); anything else is freed. */
static void wq_dispatch(void *arg) {
    wq_item_t *item = (wq_item_t *)arg;
    fl_workqueue_t *wq;

    if (!item)
        return;
    if (item->job_id) {
        wq_run_durable(item);
        return;
    }
    wq = item->owner;
    if (item->fn)
        item->fn(item->ctx);
    if (item->period_ns <= 0) {
        wq_item_free(item);
        return;
    }
    WQ_LOCK(wq);
    wq->st.periodic_inflight--;
    if (wq->st.shutdown) {
        WQ_UNLOCK(wq);
        wq_item_free(item);
        return;
    }
    item->run_at_ns = wq_next_periodic_run(wq, item);
    wq_timer_insert(wq, item);
    WQ_UNLOCK(wq);
}

static int wq_resolve_layer(const fl_wq_work_t *work) {
    int layer = FL_WQ_LAYER_BACKGROUND;

    if (!work)
        return layer;
    if (work->pq_layer >= 0 && work->pq_layer < PQ_NUM_PRIORITIES)
        layer = work->pq_layer;
    return layer;
}

static void wq_do_init(fl_workqueue_t *wq) {
    pq_init(&wq->st.pq);
    wq->st.shutdown = 0;
    wq->st.poll_depth = 0;
    wq->st.timer_count = 0;
    wq->st.periodic_inflight = 0;
    wq->st.durable_inmem = 0;
    wq->st.reserved = 0;
    wq_unregister_executor(wq); /* a re-initialised queue is no longer anyone's executor */
    __atomic_store_n(&wq->st.executor, FL_WQ_EXECUTOR_NONE, __ATOMIC_RELAXED);
    wq->st.fillers = 0;
    wq->st.poll_now_ns = -1;
    for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
        wq->st.timers[i] = NULL;
#ifdef FL_WQ_HAS_LOCK
    /* First initialisation of this storage, or of storage fl_wq_destroy ended: never an initialised mutex. */
    (void)pthread_mutex_init(&wq->st.lock, NULL);
#endif
}

#ifdef FL_WQ_HAS_LOCK
static void wq_default_init_once(void) {
    wq_do_init(&s_default_wq);
    (void)fl_wq_set_executor(&s_default_wq, FL_WQ_EXECUTOR_DEFAULT);
}
#endif

fl_workqueue_t *fl_wq_default(void) {
#ifdef FL_WQ_HAS_LOCK
    (void)pthread_once(&s_default_once, wq_default_init_once);
#else
    if (!s_default_inited) {
        wq_do_init(&s_default_wq);
        (void)fl_wq_set_executor(&s_default_wq, FL_WQ_EXECUTOR_DEFAULT);
        s_default_inited = 1;
    }
#endif
    return &s_default_wq;
}

/* Only on storage that was never initialised or that fl_wq_destroy ended: it initialises the lock. */
fl_result_t fl_wq_init(fl_workqueue_t *wq) {
    if (!wq)
        return FL_RESULT_INVAL;
    wq_do_init(wq);
    return FL_RESULT_OK;
}

/* Shutdown path: protected work runs once, everything else is dropped. */
static void wq_finish(wq_item_t *item) {
    if (item->fn && (item->flags & FL_WQ_PROTECTED))
        item->fn(item->ctx);
    wq_item_free(item);
}

/* A claim in flight finishes (it fails to publish and gives its claim back), and a resolver that picked
 * this queue out of the registry lets go of it, before the queue can be destroyed. No threads in the
 * freestanding build, so nothing is ever outstanding there. */
static void wq_await_reservations(fl_workqueue_t *wq) {
#ifdef FL_WQ_HAS_LOCK
    unsigned left;

    do {
        WQ_LOCK(wq);
        left = wq->st.reserved + __atomic_load_n(&wq->st.fillers, __ATOMIC_ACQUIRE);
        WQ_UNLOCK(wq);
        if (left)
            sched_yield();
    } while (left);
#else
    (void)wq;
#endif
}

void fl_wq_shutdown(fl_workqueue_t *wq) {
    pq_task_t task;

    if (!wq)
        return;
    wq_unregister_executor(wq);
    WQ_LOCK(wq);
    wq->st.shutdown = 1;
    while (!pq_is_empty(&wq->st.pq)) {
        if (pq_pop(&wq->st.pq, &task) != 0)
            break;
        WQ_UNLOCK(wq);
        if (task.arg)
            wq_finish((wq_item_t *)task.arg);
        WQ_LOCK(wq);
    }
    while (wq->st.timer_count > 0) {
        wq_item_t *item = wq_timer_at(wq, 0);

        wq_timer_remove_head(wq);
        WQ_UNLOCK(wq);
        wq_finish(item);
        WQ_LOCK(wq);
    }
    WQ_UNLOCK(wq);
    wq_await_reservations(wq);
}

void fl_wq_destroy(fl_workqueue_t *wq) {
    if (!wq)
        return;
#ifdef FL_WQ_HAS_LOCK
    (void)pthread_mutex_destroy(&wq->st.lock);
#endif
}

int fl_wq_pending(const fl_workqueue_t *wq) {
    int n;

    if (!wq)
        return 0;
    WQ_LOCK((fl_workqueue_t *)wq);
    n = pq_count(&wq->st.pq) + (int)wq->st.timer_count;
    WQ_UNLOCK((fl_workqueue_t *)wq);
    return n;
}

static fl_result_t wq_first_run_ns(int64_t *run_at_ns, int64_t period_ns) {
    int64_t now = 0;

    if (period_ns <= 0)
        return FL_RESULT_OK;
    if (*run_at_ns != 0) {
        if (*run_at_ns > INT64_MAX - period_ns)
            return FL_RESULT_INVAL; /* first run plus one period would not fit */
        return FL_RESULT_OK;
    }
    if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
        return FL_RESULT_ERR; /* a period needs the monotonic clock */
    if (period_ns > INT64_MAX - now)
        return FL_RESULT_INVAL;
    *run_at_ns = now + period_ns;
    return FL_RESULT_OK;
}

static wq_item_t *wq_item_from_work(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns,
                                    int64_t period_ns) {
    wq_item_t *item = wq_item_alloc();

    if (!item)
        return NULL;
    item->fn = work->fn;
    item->ctx = work->ctx;
    item->tag = work->tag;
    item->layer = wq_resolve_layer(work);
    item->run_at_ns = run_at_ns;
    item->period_ns = period_ns;
    item->flags = work->flags;
    item->owner = wq;
    return item;
}

/* Caller holds the lock. Returns FL_RESULT_OK once the item is queued. */
static fl_result_t wq_place_locked(fl_workqueue_t *wq, wq_item_t *item, unsigned limit) {
    if (wq->st.shutdown || (unsigned)pq_count(&wq->st.pq) + wq->st.timer_count + wq->st.reserved >= limit)
        return FL_RESULT_BUSY;
    if (item->run_at_ns == 0) {
        if (pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0)
            return FL_RESULT_BUSY;
        return FL_RESULT_OK;
    }
    if (wq->st.timer_count + wq->st.periodic_inflight >= FL_WQ_TIMERS_MAX)
        return FL_RESULT_BUSY;
    wq_timer_insert(wq, item);
    return FL_RESULT_OK;
}

static fl_result_t wq_submit(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns,
                             int64_t period_ns) {
    wq_item_t *item;
    fl_result_t rc;
    unsigned limit;

    if (!wq || !work || !work->fn || run_at_ns < 0 || period_ns < 0)
        return FL_RESULT_INVAL;
    rc = wq_first_run_ns(&run_at_ns, period_ns);
    if (rc != FL_RESULT_OK)
        return rc;
    item = wq_item_from_work(wq, work, run_at_ns, period_ns);
    if (!item)
        return FL_RESULT_NOMEM;
    limit = FL_WQ_PENDING_MAX - ((work->flags & FL_WQ_PROTECTED) ? 0u : FL_WQ_PROTECTED_RESERVE);

    WQ_LOCK(wq);
    rc = wq_place_locked(wq, item, limit);
    WQ_UNLOCK(wq);
    if (rc != FL_RESULT_OK)
        wq_item_free(item);
    return rc;
}

fl_result_t fl_wq_enqueue(fl_workqueue_t *wq, const fl_wq_work_t *work) {
    if (!work)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, work->run_at_ns, work->period_ns);
}

fl_result_t fl_wq_enqueue_at(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns) {
    if (!work || run_at_ns <= 0)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, run_at_ns, work->period_ns);
}

fl_result_t fl_wq_enqueue_every(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t period_ns) {
    if (!work || period_ns <= 0)
        return FL_RESULT_INVAL;
    return wq_submit(wq, work, 0, period_ns);
}



static int64_t wq_now_sec(void) {
    int64_t v = 0;
#ifdef FL_FREESTANDING
    if (fl_time_monotonic_ns(&v) == FL_RESULT_OK)
        v /= 1000000000ll; /* no calendar clock in the image: seconds since boot (its journal is memory only) */
#else
    (void)fl_time_wall_sec(&v);
#endif
    return v;
}

static void wq_copy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *x = (const unsigned char *)src;

    while (n--)
        *d++ = *x++;
}

static int wq_str_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}


#ifdef FL_FREESTANDING
#define MEMJ_MAX 16u /* the image holds a few jobs; it has no storage to keep more */
#else
#define MEMJ_MAX FL_WQ_JOURNAL_MAX_PENDING
#endif

static fl_wq_journal_record_t s_memj[MEMJ_MAX];
static unsigned char s_memj_used[MEMJ_MAX];
static uint64_t s_memj_next_id;
#ifdef FL_WQ_HAS_LOCK
static pthread_mutex_t s_memj_mu = PTHREAD_MUTEX_INITIALIZER;
#define MEMJ_LOCK() (void)pthread_mutex_lock(&s_memj_mu)
#define MEMJ_UNLOCK() (void)pthread_mutex_unlock(&s_memj_mu)
#else
#define MEMJ_LOCK() ((void)0)
#define MEMJ_UNLOCK() ((void)0)
#endif

static int memj_terminal(fl_wq_state_t st) {
    return fl_wq_state_terminal((uint8_t)st) != 0;
}

static int memj_find(uint64_t id) {
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        if (s_memj_used[i] && s_memj[i].id == id)
            return (int)i;
    return -1;
}

/* Caller holds the memory journal lock. Returns the slot whose key matches, or -1. */
static int memj_find_idem(const uint8_t key[16]) {
    for (unsigned i = 0; i < MEMJ_MAX; i++) {
        unsigned k = 0;

        if (!s_memj_used[i])
            continue;
        while (k < 16 && s_memj[i].idem_key[k] == key[k])
            k++;
        if (k == 16)
            return (int)i;
    }
    return -1;
}

/* Caller holds the memory journal lock. A free slot, else the oldest finished record;
 * waiting work is never evicted. Returns -1 when neither exists. */
static int memj_pick_slot(void) {
    int slot = -1;
    uint64_t oldest = 0;

    for (unsigned i = 0; i < MEMJ_MAX && slot < 0; i++)
        if (!s_memj_used[i])
            slot = (int)i;
    if (slot >= 0)
        return slot;
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        if (memj_terminal(s_memj[i].state) && (slot < 0 || s_memj[i].id < oldest)) {
            slot = (int)i;
            oldest = s_memj[i].id;
        }
    return slot;
}

/* Caller holds the memory journal lock. PENDING, QUEUED, RUNNING and HELD records: the backlog bound. */
static unsigned memj_outstanding(void) {
    unsigned c = 0;

    for (unsigned i = 0; i < MEMJ_MAX; i++)
        c += s_memj_used[i] && !memj_terminal(s_memj[i].state);
    return c;
}

static fl_result_t memj_append(void *ctx, const fl_wq_journal_record_t *r, unsigned max_outstanding,
                               uint64_t *id_out, int *existing) {
    int slot;

    (void)ctx;
    MEMJ_LOCK();
    slot = memj_find_idem(r->idem_key);
    if (slot >= 0) {
        *id_out = s_memj[slot].id;
        *existing = 1;
        MEMJ_UNLOCK();
        return FL_RESULT_OK;
    }
    slot = memj_outstanding() >= max_outstanding ? -1 : memj_pick_slot();
    if (slot < 0) {
        MEMJ_UNLOCK();
        return FL_RESULT_BUSY;
    }
    wq_copy(&s_memj[slot], r, sizeof(*r));
    s_memj[slot].id = ++s_memj_next_id;
    s_memj[slot].state = FL_WQ_ST_PENDING;
    s_memj[slot].attempts = 0;
    s_memj_used[slot] = 1;
    *id_out = s_memj[slot].id;
    *existing = 0;
    MEMJ_UNLOCK();
    return FL_RESULT_OK;
}

static fl_result_t memj_mark(void *ctx, uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    int i;

    (void)ctx;
    MEMJ_LOCK();
    i = memj_find(id);
    if (i < 0 || s_memj[i].state != from) {
        MEMJ_UNLOCK();
        return FL_RESULT_BUSY;
    }
    s_memj[i].state = to;
    if (to == FL_WQ_ST_RUNNING)
        s_memj[i].attempts++;
    MEMJ_UNLOCK();
    return FL_RESULT_OK;
}

static fl_result_t memj_load(void *ctx, uint64_t id, fl_wq_journal_record_t *out) {
    int i;

    (void)ctx;
    MEMJ_LOCK();
    i = memj_find(id);
    if (i >= 0)
        wq_copy(out, &s_memj[i], sizeof(*out));
    MEMJ_UNLOCK();
    return i >= 0 ? FL_RESULT_OK : FL_RESULT_NOENT;
}

static int memj_after(const fl_wq_journal_record_t *r, const fl_wq_journal_record_t *c) {
    return !c || r->layer > c->layer || (r->layer == c->layer && r->id > c->id);
}

static int memj_match(const fl_wq_journal_record_t *r, fl_wq_state_t st, const char *owner, uint32_t executor) {
    return r->state == st && (!owner || wq_str_eq(r->owner, owner)) &&
           (executor == FL_WQ_EXECUTOR_ANY || r->executor == executor);
}

static fl_result_t memj_scan(void *ctx, fl_wq_state_t st, const char *owner, uint32_t executor,
                             const fl_wq_journal_record_t *after, unsigned max, fl_wq_journal_record_t *out, unsigned *n) {
    unsigned got = 0;

    (void)ctx;
    MEMJ_LOCK();
    while (got < max) { /* repeatedly take the smallest (layer, id) past the last one taken */
        int best = -1;
        const fl_wq_journal_record_t *cur = got ? &out[got - 1] : after;

        for (unsigned i = 0; i < MEMJ_MAX; i++) {
            if (!s_memj_used[i] || !memj_match(&s_memj[i], st, owner, executor) || !memj_after(&s_memj[i], cur))
                continue;
            if (best < 0 || s_memj[i].layer < s_memj[best].layer ||
                (s_memj[i].layer == s_memj[best].layer && s_memj[i].id < s_memj[best].id))
                best = (int)i;
        }
        if (best < 0)
            break;
        wq_copy(&out[got++], &s_memj[best], sizeof(out[0]));
    }
    MEMJ_UNLOCK();
    *n = got;
    return FL_RESULT_OK;
}

static unsigned memj_count(void *ctx, fl_wq_state_t st, const char *owner, uint32_t executor) {
    unsigned c = 0;

    (void)ctx;
    MEMJ_LOCK();
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        if (s_memj_used[i] && memj_match(&s_memj[i], st, owner, executor))
            c++;
    MEMJ_UNLOCK();
    return c;
}

static const fl_wq_journal_ops_t s_memj_ops = {NULL, memj_append, memj_mark, memj_load, memj_scan, memj_count};
static const fl_wq_journal_ops_t *s_journal = &s_memj_ops;

const fl_wq_journal_ops_t *fl_wq_journal_mem(void) {
    return &s_memj_ops;
}

void fl_wq_journal_mem_reset(void) {
    MEMJ_LOCK();
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        s_memj_used[i] = 0;
    s_memj_next_id = 0;
    MEMJ_UNLOCK();
}

static void wq_retry_clear(void);

const fl_wq_journal_ops_t *fl_wq_set_journal(const fl_wq_journal_ops_t *ops) {
    const fl_wq_journal_ops_t *prev = s_journal;

    s_journal = ops ? ops : &s_memj_ops;
    wq_retry_clear(); /* an entry names a row of the journal that was in force */
    return prev;
}


static struct {
    uint32_t tag;
    fl_wq_handler_fn_t fn;
    uint8_t recover;
} s_handlers[FL_WQ_HANDLERS_MAX];
static unsigned s_handler_count;

fl_result_t fl_wq_register_handler(uint32_t tag, fl_wq_handler_fn_t fn, uint8_t recover) {
    if (!fn || (recover != FL_WQ_RECOVER_AUTO && recover != FL_WQ_RECOVER_ASK))
        return FL_RESULT_INVAL;
    for (unsigned i = 0; i < s_handler_count; i++)
        if (s_handlers[i].tag == tag)
            return FL_RESULT_BUSY;
    if (s_handler_count >= FL_WQ_HANDLERS_MAX)
        return FL_RESULT_NOMEM;
    s_handlers[s_handler_count].tag = tag;
    s_handlers[s_handler_count].fn = fn;
    s_handlers[s_handler_count].recover = recover;
    s_handler_count++;
    return FL_RESULT_OK;
}

static int wq_handler_index(uint32_t tag) {
    for (unsigned i = 0; i < s_handler_count; i++)
        if (s_handlers[i].tag == tag)
            return (int)i;
    return -1;
}


/* Every state change the queue asks a journal for goes through here: a move the proven model does not
 * allow is refused before the journal sees it (and counted: it is a bug in this file, never a race). */
static fl_result_t wq_mark(const fl_wq_journal_ops_t *j, uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    if (!fl_wq_transition_allowed((uint8_t)from, (uint8_t)to)) {
        (void)__atomic_fetch_add(&s_illegal_moves, 1u, __ATOMIC_RELAXED);
        return FL_RESULT_INVAL;
    }
    return j->mark(j->ctx, id, from, to);
}

/* Completion writes the journal refused. The handler already ran, so the write is retried and the
 * handler is not: the record stays RUNNING until it lands. */
#define WQ_RETRY_MAX 16u
static struct {
    uint64_t id;
    uint8_t from, to, used;
} s_retry[WQ_RETRY_MAX];
static unsigned s_retry_lost, s_stuck_claims;

unsigned fl_wq_retry_marks_pending(void) {
    unsigned n = 0;

    WQ_GLOBAL_LOCK();
    for (unsigned i = 0; i < WQ_RETRY_MAX; i++)
        n += s_retry[i].used;
    WQ_GLOBAL_UNLOCK();
    return n;
}

unsigned fl_wq_retry_marks_lost(void) {
    return __atomic_load_n(&s_retry_lost, __ATOMIC_RELAXED);
}

unsigned fl_wq_stuck_claims(void) {
    return __atomic_load_n(&s_stuck_claims, __ATOMIC_RELAXED);
}

static void wq_retry_clear(void) {
    WQ_GLOBAL_LOCK();
    for (unsigned i = 0; i < WQ_RETRY_MAX; i++)
        s_retry[i].used = 0;
    WQ_GLOBAL_UNLOCK();
}

static void wq_retry_add(uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    unsigned i = 0;

    WQ_GLOBAL_LOCK();
    while (i < WQ_RETRY_MAX && s_retry[i].used)
        i++;
    if (i < WQ_RETRY_MAX) {
        s_retry[i].id = id;
        s_retry[i].from = (uint8_t)from;
        s_retry[i].to = (uint8_t)to;
        s_retry[i].used = 1;
    } else {
        (void)__atomic_fetch_add(&s_retry_lost, 1u, __ATOMIC_RELAXED);
    }
    WQ_GLOBAL_UNLOCK();
}

/* A lost race (BUSY) means another path moved the job: nothing is owed. Any other failure is kept. */
static void wq_settle(const fl_wq_journal_ops_t *j, uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    fl_result_t rc = wq_mark(j, id, from, to);

    if (rc != FL_RESULT_OK && rc != FL_RESULT_BUSY)
        wq_retry_add(id, from, to);
}

static void wq_retry_marks(const fl_wq_journal_ops_t *j) {
    for (unsigned i = 0; i < WQ_RETRY_MAX; i++) {
        uint64_t id = 0;
        uint8_t from = 0, to = 0;

        WQ_GLOBAL_LOCK();
        if (s_retry[i].used) {
            id = s_retry[i].id;
            from = s_retry[i].from;
            to = s_retry[i].to;
            s_retry[i].used = 0;
        }
        WQ_GLOBAL_UNLOCK();
        if (id)
            wq_settle(j, id, (fl_wq_state_t)from, (fl_wq_state_t)to);
    }
}

/* Caller holds the lock. Reservations count against the ordinary cap, the protected reserve and the
 * durable window alike, so the slot is there when the claimed job is published. */
static int wq_room_for_durable_locked(const fl_workqueue_t *wq) {
    unsigned used = (unsigned)pq_count(&wq->st.pq) + wq->st.timer_count + wq->st.reserved;

    return !wq->st.shutdown && used < FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE &&
           wq->st.durable_inmem + wq->st.reserved < FL_WQ_PENDING_MAX / 2u;
}

static fl_result_t wq_reserve(fl_workqueue_t *wq) {
    int ok;

    WQ_LOCK(wq);
    ok = wq_room_for_durable_locked(wq);
    if (ok)
        wq->st.reserved++;
    WQ_UNLOCK(wq);
    return ok ? FL_RESULT_OK : FL_RESULT_BUSY;
}

static void wq_release(fl_workqueue_t *wq) {
    WQ_LOCK(wq);
    if (wq->st.reserved)
        wq->st.reserved--;
    WQ_UNLOCK(wq);
}

/* Put a claimed record into the MLQ under the reservation. On success the reservation becomes a staged
 * item; on failure the caller still holds it and releases it. */
static fl_result_t wq_publish(fl_workqueue_t *wq, const fl_wq_journal_record_t *r) {
    wq_item_t *item = wq_item_alloc();
    fl_result_t rc = FL_RESULT_OK;

    if (!item)
        return FL_RESULT_NOMEM;
    item->job_id = r->id;
    item->layer = r->layer >= 0 && r->layer < PQ_NUM_PRIORITIES ? r->layer : FL_WQ_LAYER_BACKGROUND;
    item->owner = wq;
    WQ_LOCK(wq);
    if (wq->st.shutdown || pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0) {
        rc = FL_RESULT_BUSY;
    } else {
        wq->st.durable_inmem++;
        wq->st.reserved--;
    }
    WQ_UNLOCK(wq);
    if (rc != FL_RESULT_OK)
        wq_item_free(item);
    return rc;
}

/* QUEUED -> PENDING. A lost race means recovery or another path already moved it; a failed write
 * leaves the record QUEUED (still durable, repaired by recovery) and is counted. */
static fl_result_t wq_unclaim(const fl_wq_journal_record_t *r) {
    fl_result_t rc = wq_mark(s_journal, r->id, FL_WQ_ST_QUEUED, FL_WQ_ST_PENDING);

    if (rc == FL_RESULT_BUSY)
        return FL_RESULT_OK;
    if (rc != FL_RESULT_OK)
        (void)__atomic_fetch_add(&s_stuck_claims, 1u, __ATOMIC_RELAXED);
    return rc;
}

/* Compare-and-set the oldest PENDING record of this executor to QUEUED. A record another thread
 * claimed first is skipped; NOENT when none is left. */
static fl_result_t wq_claim_next(uint32_t executor, fl_wq_journal_record_t *out) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t page[8], cursor;
    unsigned n, have = 0;

    for (;;) {
        fl_result_t rc =
            j->scan(j->ctx, FL_WQ_ST_PENDING, NULL, executor, have ? &cursor : NULL, 8, page, &n);

        if (rc != FL_RESULT_OK)
            return rc;
        if (n == 0)
            return FL_RESULT_NOENT;
        for (unsigned i = 0; i < n; i++) {
            rc = wq_mark(j, page[i].id, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED);
            if (rc == FL_RESULT_OK) {
                wq_copy(out, &page[i], sizeof(*out));
                out->state = FL_WQ_ST_QUEUED;
                return FL_RESULT_OK;
            }
            if (rc != FL_RESULT_BUSY)
                return rc;
        }
        wq_copy(&cursor, &page[n - 1], sizeof(cursor));
        have = 1;
    }
}

/* Stage one job. OK: staged. NOENT: no room or nothing to stage. Anything else is the failure, after
 * the reservation is released and a successful claim is given back. */
static fl_result_t wq_stage_one(fl_workqueue_t *wq) {
    fl_wq_journal_record_t r;
    fl_result_t rc, rb;

    if (wq_reserve(wq) != FL_RESULT_OK)
        return FL_RESULT_NOENT;
    rc = wq_claim_next(wq_executor_of(wq), &r);
    if (rc != FL_RESULT_OK) {
        wq_release(wq);
        return rc;
    }
    rc = wq_publish(wq, &r);
    if (rc == FL_RESULT_OK)
        return rc;
    wq_release(wq);
    rb = wq_unclaim(&r);
    return rb != FL_RESULT_OK ? rb : rc;
}

/* The in-memory queue is a cache of the journal: keep up to half the pending cap staged. Returns the
 * first failure; "no room" and "nothing waiting" are not failures. */
static fl_result_t wq_fill_window(fl_workqueue_t *wq) {
    fl_result_t rc;

    wq_retry_marks(s_journal);
    if (wq_executor_of(wq) == FL_WQ_EXECUTOR_NONE)
        return FL_RESULT_OK;
    while ((rc = wq_stage_one(wq)) == FL_RESULT_OK)
        ;
    return rc == FL_RESULT_NOENT ? FL_RESULT_OK : rc;
}

static void wq_run_handler(const fl_wq_journal_ops_t *j, const fl_wq_journal_record_t *r, int h) {
    fl_result_t rc = wq_mark(j, r->id, FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING);
    fl_wq_state_t next;

    if (rc != FL_RESULT_OK) { /* RUNNING is written before the handler: without it the handler does not run */
        if (rc != FL_RESULT_BUSY)
            (void)wq_unclaim(r);
        return;
    }
    rc = s_handlers[h].fn(r->payload, r->payload_len, r->idem_key);
    next = rc == FL_RESULT_OK ? FL_WQ_ST_DONE
           : r->attempts + 1u < r->max_attempts ? FL_WQ_ST_PENDING
                                                 : FL_WQ_ST_FAILED;
    wq_settle(j, r->id, FL_WQ_ST_RUNNING, next);
}

static void wq_run_queued(const fl_workqueue_t *wq, const fl_wq_journal_ops_t *j, const fl_wq_journal_record_t *r) {
    int h = wq_handler_index(r->tag);

    if (r->executor != wq_executor_of(wq))
        (void)wq_unclaim(r); /* staged by a queue that has since changed executor */
    else if (r->deadline && wq_now_sec() > r->deadline)
        wq_settle(j, r->id, FL_WQ_ST_QUEUED, FL_WQ_ST_EXPIRED); /* stale: never run silently */
    else if (h < 0)
        wq_settle(j, r->id, FL_WQ_ST_QUEUED, FL_WQ_ST_FAILED);
    else
        wq_run_handler(j, r, h);
}

static void wq_run_durable(wq_item_t *item) {
    fl_workqueue_t *wq = item->owner;
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t r;

    if (j->load(j->ctx, item->job_id, &r) == FL_RESULT_OK && r.state == FL_WQ_ST_QUEUED)
        wq_run_queued(wq, j, &r);
    WQ_LOCK(wq);
    if (wq->st.durable_inmem)
        wq->st.durable_inmem--;
    WQ_UNLOCK(wq);
    wq_item_free(item);
}

static fl_result_t wq_check_owner_length(const char *owner) {
    size_t len = 0;

    if (!owner || !owner[0])
        return FL_RESULT_OK;
    while (len < FL_HERITAGE_KEY_ID_MAX && owner[len]) /* the bound first: never read past the buffer */
        len++;
    return len >= FL_HERITAGE_KEY_ID_MAX ? FL_RESULT_INVAL : FL_RESULT_OK;
}

static void wq_zero_record(fl_wq_journal_record_t *r) {
    unsigned char *b = (unsigned char *)r;

    for (size_t k = 0; k < sizeof(*r); k++)
        b[k] = 0;
}

static fl_result_t wq_build_record(fl_wq_journal_record_t *r, uint32_t tag, int layer, int handler,
                                   const char *owner, const void *payload, size_t n,
                                   const uint8_t idem_key[16], int64_t ttl_sec) {
    wq_zero_record(r);
    wq_copy(r->idem_key, idem_key, 16);
    r->tag = tag;
    r->layer = layer;
    r->recover = s_handlers[handler].recover;
    if (owner)
        for (size_t k = 0; owner[k]; k++)
            r->owner[k] = owner[k];
    r->state = FL_WQ_ST_PENDING;
    r->max_attempts = FL_WQ_MAX_ATTEMPTS;
    r->accepted_at = wq_now_sec();
    r->deadline = 0;
    if (ttl_sec > 0) {
        if (r->accepted_at > 0 && ttl_sec > INT64_MAX - r->accepted_at)
            return FL_RESULT_INVAL; /* the deadline would overflow: refuse, never wrap into the past */
        r->deadline = r->accepted_at + ttl_sec;
    }
    r->payload_len = (uint32_t)n;
    if (n)
        wq_copy(r->payload, payload, n);
    return FL_RESULT_OK;
}

fl_result_t fl_wq_enqueue_durable(fl_workqueue_t *wq, uint32_t tag, int layer, const char *owner,
                                  const void *payload, size_t n, const uint8_t idem_key[16],
                                  int64_t ttl_sec) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t r;
    int h, existing = 0;
    uint64_t id = 0;
    fl_result_t rc;

    if (!wq || !idem_key || n > FL_WQ_PAYLOAD_MAX || (n && !payload) || layer < 0 || layer >= PQ_NUM_PRIORITIES)
        return FL_RESULT_INVAL;
    if (wq_check_owner_length(owner) != FL_RESULT_OK || wq_executor_of(wq) == FL_WQ_EXECUTOR_NONE)
        return FL_RESULT_INVAL;
    h = wq_handler_index(tag);
    if (h < 0)
        return FL_RESULT_NOENT; /* nothing could run it after a restart */
    if (wq->st.shutdown)
        return FL_RESULT_BUSY;
    rc = wq_build_record(&r, tag, layer, h, owner, payload, n, idem_key, ttl_sec);
    if (rc != FL_RESULT_OK)
        return rc;
    r.executor = wq_executor_of(wq);

    /* One step in the backend: a repeated key, then the backlog bound, then the append. Not durable
     * means not accepted. */
    rc = j->append(j->ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &existing);
    if (rc != FL_RESULT_OK || existing)
        return rc;
    return wq_fill_window(wq) == FL_RESULT_OK ? FL_RESULT_OK : FL_WQ_ACCEPTED_UNSTAGED;
}

/* Visit every record in `state` for this owner and executor, page by page. A lost race (BUSY) is
 * normal; the first other failure is kept and the walk goes on, so committed progress stays and the
 * caller learns that something failed. */
typedef fl_result_t (*wq_visit_fn)(const fl_wq_journal_record_t *r, void *arg);

static fl_result_t wq_walk(fl_wq_state_t state, const char *owner, uint32_t executor, wq_visit_fn visit,
                           void *arg) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t batch[16], cursor;
    fl_result_t first = FL_RESULT_OK, rc;
    unsigned n, have = 0;

    for (;;) {
        rc = j->scan(j->ctx, state, owner, executor, have ? &cursor : NULL, 16, batch, &n);
        if (rc != FL_RESULT_OK)
            return first != FL_RESULT_OK ? first : rc;
        if (n == 0)
            return first;
        for (unsigned i = 0; i < n; i++) {
            rc = visit(&batch[i], arg);
            if (rc != FL_RESULT_OK && rc != FL_RESULT_BUSY && first == FL_RESULT_OK)
                first = rc;
        }
        wq_copy(&cursor, &batch[n - 1], sizeof(cursor));
        have = 1;
    }
}

/* A job cut off mid-run (RUNNING) is retried, or FAILED once its attempts are spent; a staged one
 * (QUEUED) simply goes back to waiting. */
static fl_result_t wq_visit_reset(const fl_wq_journal_record_t *r, void *arg) {
    fl_wq_state_t to = r->state == FL_WQ_ST_RUNNING && r->attempts >= r->max_attempts ? FL_WQ_ST_FAILED
                                                                                      : FL_WQ_ST_PENDING;

    (void)arg;
    return wq_mark(s_journal, r->id, r->state, to);
}

/* Stale work is EXPIRED and RECOVER_ASK work is HELD; the rest stays PENDING for the window. */
static fl_result_t wq_visit_triage(const fl_wq_journal_record_t *r, void *arg) {
    int64_t now = *(const int64_t *)arg;

    if (r->deadline && now > r->deadline)
        return wq_mark(s_journal, r->id, FL_WQ_ST_PENDING, FL_WQ_ST_EXPIRED);
    if (r->recover == FL_WQ_RECOVER_ASK)
        return wq_mark(s_journal, r->id, FL_WQ_ST_PENDING, FL_WQ_ST_HELD);
    return FL_RESULT_OK;
}

fl_result_t fl_wq_recover(fl_workqueue_t *wq) {
    int64_t now = wq_now_sec();
    uint32_t ex;
    fl_result_t first, rc;

    if (!wq)
        return FL_RESULT_INVAL;
    ex = wq_executor_of(wq);
    if (ex == FL_WQ_EXECUTOR_NONE)
        return FL_RESULT_OK;
    first = wq_walk(FL_WQ_ST_RUNNING, NULL, ex, wq_visit_reset, NULL);
    rc = wq_walk(FL_WQ_ST_QUEUED, NULL, ex, wq_visit_reset, NULL);
    first = first != FL_RESULT_OK ? first : rc;
    rc = wq_walk(FL_WQ_ST_PENDING, NULL, ex, wq_visit_triage, &now);
    first = first != FL_RESULT_OK ? first : rc;
    rc = wq_fill_window(wq);
    return first != FL_RESULT_OK ? first : rc;
}

unsigned fl_wq_held_count(const char *owner) {
    const fl_wq_journal_ops_t *j = s_journal;

    return j->count(j->ctx, FL_WQ_ST_HELD, owner ? owner : "", FL_WQ_EXECUTOR_ANY);
}

static fl_result_t wq_visit_tally_unowned(const fl_wq_journal_record_t *r, void *arg) {
    if (!wq_executor_known(r->executor))
        (*(unsigned *)arg)++;
    return FL_RESULT_OK;
}

fl_result_t fl_wq_unowned_records(unsigned *count) {
    static const fl_wq_state_t outstanding[] = {FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING, FL_WQ_ST_HELD};
    fl_result_t first = FL_RESULT_OK, rc;
    unsigned n = 0;

    if (!count)
        return FL_RESULT_INVAL;
    for (unsigned i = 0; i < sizeof(outstanding) / sizeof(outstanding[0]); i++) {
        rc = wq_walk(outstanding[i], NULL, FL_WQ_EXECUTOR_ANY, wq_visit_tally_unowned, &n);
        first = first != FL_RESULT_OK ? first : rc;
    }
    *count = n;
    return first;
}

/* Only a registered executor's job moves; the rest stay HELD where fl_wq_unowned_records sees them. */
static fl_result_t wq_visit_resolve(const fl_wq_journal_record_t *r, void *arg) {
    int run_now = *(const int *)arg;

    if (!wq_executor_known(r->executor))
        return FL_RESULT_OK;
    return wq_mark(s_journal, r->id, FL_WQ_ST_HELD, run_now ? FL_WQ_ST_PENDING : FL_WQ_ST_SKIPPED);
}

/* Each queue is pinned (fillers) while the registry lock is held, so fl_wq_shutdown, which unregisters it
 * under that lock and then waits for the pins, cannot return before the fill is done with the queue. */
static void wq_fill_registered(void) {
    fl_workqueue_t *queues[FL_WQ_EXECUTORS_MAX];
    unsigned n;

    WQ_GLOBAL_LOCK();
    n = s_executor_count;
    for (unsigned i = 0; i < n; i++) {
        queues[i] = s_executors[i].wq;
        (void)__atomic_fetch_add(&queues[i]->st.fillers, 1u, __ATOMIC_ACQ_REL);
    }
    WQ_GLOBAL_UNLOCK();
    for (unsigned i = 0; i < n; i++) {
        (void)wq_fill_window(queues[i]);
        (void)__atomic_fetch_sub(&queues[i]->st.fillers, 1u, __ATOMIC_RELEASE);
    }
}

fl_result_t fl_wq_resolve_held(const char *owner, int run_now) {
    fl_result_t rc;

    if (!owner)
        return FL_RESULT_INVAL;
    rc = wq_walk(FL_WQ_ST_HELD, owner, FL_WQ_EXECUTOR_ANY, wq_visit_resolve, &run_now);
    if (run_now)
        wq_fill_registered();
    return rc;
}

/* Move every due timer into the MLQ, earliest first. Caller holds the lock. */
static void wq_promote_due(fl_workqueue_t *wq, int64_t now_ns) {
    while (wq->st.timer_count > 0) {
        wq_item_t *item = wq_timer_at(wq, 0);

        if (item->run_at_ns > now_ns)
            break;
        if (pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0)
            break; /* MLQ full: stays due, tried again next poll */
        wq_timer_remove_head(wq);
        if (item->period_ns > 0)
            wq->st.periodic_inflight++;
    }
}

unsigned fl_wq_poll_at(fl_workqueue_t *wq, unsigned max_items, int64_t now_ns) {
    unsigned ran = 0;

    if (!wq || max_items == 0)
        return 0;

    (void)wq_fill_window(wq);
    WQ_LOCK(wq);
#ifdef FL_WQ_DEBUG_REENTRANCY
    assert(wq->st.poll_depth == 0 &&
           "fl_wq_poll: one poller per queue — no nested poll");
#endif
    wq->st.poll_depth++;
    wq->st.poll_now_ns = now_ns;
    if (now_ns >= 0)
        wq_promote_due(wq, now_ns);

    while (ran < max_items && !pq_is_empty(&wq->st.pq)) {
        pq_task_t task;

        if (pq_pop(&wq->st.pq, &task) != 0)
            break;
        WQ_UNLOCK(wq);
        if (task.fn)
            task.fn(task.arg);
        WQ_LOCK(wq);
        ran++;
    }

    wq->st.poll_depth--;
    WQ_UNLOCK(wq);
    return ran;
}

unsigned fl_wq_poll(fl_workqueue_t *wq, unsigned max_items) {
    int64_t now_ns = -1;

    if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
        now_ns = -1;
    return fl_wq_poll_at(wq, max_items, now_ns);
}

static int wq_mlq_empty(fl_workqueue_t *wq) {
    int empty;

    WQ_LOCK(wq);
    empty = pq_is_empty(&wq->st.pq);
    WQ_UNLOCK(wq);
    return empty;
}

fl_result_t fl_wq_drain(fl_workqueue_t *wq, unsigned timeout_ms) {
    int64_t deadline_ns = 0;
    int have_deadline = 0;

    if (!wq)
        return FL_RESULT_INVAL;

    if (timeout_ms > 0) {
        int64_t start_ns = 0;

        if (fl_time_monotonic_ns(&start_ns) == FL_RESULT_OK) {
            deadline_ns = start_ns + (int64_t)timeout_ms * 1000000LL;
            have_deadline = 1;
        }
        /* else: no monotonic clock on this path — fall through to best-effort single poll */
    }

    for (;;) {
        if (wq_mlq_empty(wq))
            return FL_RESULT_OK;

        (void)fl_wq_poll(wq, FL_WQ_PENDING_MAX);

        if (timeout_ms == 0)
            continue;

        if (!have_deadline) {
            if (wq_mlq_empty(wq))
                return FL_RESULT_OK;
            return FL_RESULT_BUSY;
        }

        {
            int64_t now_ns = 0;

            if (fl_time_monotonic_ns(&now_ns) != FL_RESULT_OK)
                return FL_RESULT_BUSY;
            if (now_ns >= deadline_ns)
                return FL_RESULT_BUSY;
        }
    }
}
