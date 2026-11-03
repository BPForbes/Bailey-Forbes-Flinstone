#include "workqueue.h"

#include "timekeeping.h"

#ifndef FL_FREESTANDING
#include <assert.h>
#include <stdlib.h>
#endif

#if !defined(NDEBUG) && !defined(FL_FREESTANDING)
#define FL_WQ_DEBUG_REENTRANCY 1
#endif

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
static void wq_fill_window(fl_workqueue_t *wq);

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
    {
        int64_t base = wq->st.poll_now_ns >= 0 ? wq->st.poll_now_ns : item->run_at_ns;
        int64_t next;

        /* a huge period must not wrap negative (that would run the item on every poll) */
        if (__builtin_add_overflow(base, item->period_ns, &next))
            next = INT64_MAX;
        item->run_at_ns = next;
    }
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
    wq->st.poll_now_ns = -1;
    for (unsigned i = 0; i < FL_WQ_TIMERS_MAX; i++)
        wq->st.timers[i] = NULL;
#ifdef FL_WQ_HAS_LOCK
    /* Not destroyed at shutdown: the queue may be re-initialised in place, and a held-open
     * mutex object owns no resource on the platforms this builds for. */
    (void)pthread_mutex_init(&wq->st.lock, NULL);
#endif
}

#ifdef FL_WQ_HAS_LOCK
static void wq_default_init_once(void) {
    wq_do_init(&s_default_wq);
}
#endif

fl_workqueue_t *fl_wq_default(void) {
#ifdef FL_WQ_HAS_LOCK
    (void)pthread_once(&s_default_once, wq_default_init_once);
#else
    if (!s_default_inited) {
        wq_do_init(&s_default_wq);
        s_default_inited = 1;
    }
#endif
    return &s_default_wq;
}

/* Only on a queue nobody is using: it re-initialises the lock and drops what is queued. */
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

void fl_wq_shutdown(fl_workqueue_t *wq) {
    pq_task_t task;

    if (!wq)
        return;
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

static fl_result_t wq_submit(fl_workqueue_t *wq, const fl_wq_work_t *work, int64_t run_at_ns,
                             int64_t period_ns) {
    wq_item_t *item;
    unsigned limit;

    if (!wq || !work || !work->fn || run_at_ns < 0 || period_ns < 0)
        return FL_RESULT_INVAL;
    if (period_ns > 0 && run_at_ns == 0) {
        int64_t now = 0;

        if (fl_time_monotonic_ns(&now) != FL_RESULT_OK)
            return FL_RESULT_ERR; /* a period needs P1-7 */
        if (period_ns > INT64_MAX - now)
            return FL_RESULT_INVAL;
        run_at_ns = now + period_ns;
    } else if (period_ns > 0 && run_at_ns > INT64_MAX - period_ns) {
        return FL_RESULT_INVAL; /* first run plus one period would not fit */
    }

    item = wq_item_alloc();
    if (!item)
        return FL_RESULT_NOMEM;
    item->fn = work->fn;
    item->ctx = work->ctx;
    item->tag = work->tag;
    item->layer = wq_resolve_layer(work);
    item->run_at_ns = run_at_ns;
    item->period_ns = period_ns;
    item->flags = work->flags;
    item->owner = wq;

    limit = FL_WQ_PENDING_MAX - ((work->flags & FL_WQ_PROTECTED) ? 0u : FL_WQ_PROTECTED_RESERVE);

    WQ_LOCK(wq);
    if (wq->st.shutdown || (unsigned)pq_count(&wq->st.pq) + wq->st.timer_count >= limit) {
        WQ_UNLOCK(wq);
        wq_item_free(item);
        return FL_RESULT_BUSY;
    }
    if (run_at_ns == 0) {
        if (pq_push(&wq->st.pq, item->layer, wq_dispatch, item) < 0) {
            WQ_UNLOCK(wq);
            wq_item_free(item);
            return FL_RESULT_BUSY;
        }
    } else {
        if (wq->st.timer_count + wq->st.periodic_inflight >= FL_WQ_TIMERS_MAX) {
            WQ_UNLOCK(wq);
            wq_item_free(item);
            return FL_RESULT_BUSY;
        }
        wq_timer_insert(wq, item);
    }
    WQ_UNLOCK(wq);
    return FL_RESULT_OK;
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


/* ===== Durable work (FL-25) ============================================================================ */

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

/* ---- the default journal: memory only ---- */

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
    return st == FL_WQ_ST_DONE || st == FL_WQ_ST_FAILED || st == FL_WQ_ST_EXPIRED || st == FL_WQ_ST_SKIPPED;
}

static int memj_find(uint64_t id) {
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        if (s_memj_used[i] && s_memj[i].id == id)
            return (int)i;
    return -1;
}

static fl_result_t memj_append(void *ctx, const fl_wq_journal_record_t *r, uint64_t *id_out, int *existing) {
    int slot = -1;
    uint64_t oldest = 0;

    (void)ctx;
    MEMJ_LOCK();
    for (unsigned i = 0; i < MEMJ_MAX; i++) {
        if (s_memj_used[i]) {
            unsigned k = 0;

            while (k < 16 && s_memj[i].idem_key[k] == r->idem_key[k])
                k++;
            if (k == 16) {
                *id_out = s_memj[i].id;
                *existing = 1;
                MEMJ_UNLOCK();
                return FL_RESULT_OK;
            }
        }
    }
    for (unsigned i = 0; i < MEMJ_MAX && slot < 0; i++)
        if (!s_memj_used[i])
            slot = (int)i;
    if (slot < 0) { /* full: reuse the oldest finished record; waiting work is never evicted */
        for (unsigned i = 0; i < MEMJ_MAX; i++)
            if (memj_terminal(s_memj[i].state) && (slot < 0 || s_memj[i].id < oldest)) {
                slot = (int)i;
                oldest = s_memj[i].id;
            }
    }
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

static int memj_match(const fl_wq_journal_record_t *r, fl_wq_state_t st, const char *owner) {
    return r->state == st && (!owner || wq_str_eq(r->owner, owner));
}

static fl_result_t memj_scan(void *ctx, fl_wq_state_t st, const char *owner, const fl_wq_journal_record_t *after,
                             unsigned max, fl_wq_journal_record_t *out, unsigned *n) {
    unsigned got = 0;

    (void)ctx;
    MEMJ_LOCK();
    while (got < max) { /* repeatedly take the smallest (layer, id) past the last one taken */
        int best = -1;
        const fl_wq_journal_record_t *cur = got ? &out[got - 1] : after;

        for (unsigned i = 0; i < MEMJ_MAX; i++) {
            if (!s_memj_used[i] || !memj_match(&s_memj[i], st, owner) || !memj_after(&s_memj[i], cur))
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

static unsigned memj_count(void *ctx, fl_wq_state_t st, const char *owner) {
    unsigned c = 0;

    (void)ctx;
    MEMJ_LOCK();
    for (unsigned i = 0; i < MEMJ_MAX; i++)
        if (s_memj_used[i] && memj_match(&s_memj[i], st, owner))
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

const fl_wq_journal_ops_t *fl_wq_set_journal(const fl_wq_journal_ops_t *ops) {
    const fl_wq_journal_ops_t *prev = s_journal;

    s_journal = ops ? ops : &s_memj_ops;
    return prev;
}

/* ---- handlers ---- */

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

/* ---- staging, running, recovery ---- */

/* Put one journal record into the MLQ. The record is already QUEUED; the caller undoes that if this
 * returns nonzero. */
static int wq_stage(fl_workqueue_t *wq, const fl_wq_journal_record_t *r) {
    wq_item_t *item = wq_item_alloc();
    int ok;

    if (!item)
        return -1;
    item->job_id = r->id;
    item->layer = r->layer >= 0 && r->layer < PQ_NUM_PRIORITIES ? r->layer : FL_WQ_LAYER_BACKGROUND;
    item->owner = wq;
    WQ_LOCK(wq);
    ok = !wq->st.shutdown && pq_push(&wq->st.pq, item->layer, wq_dispatch, item) >= 0;
    if (ok)
        wq->st.durable_inmem++;
    WQ_UNLOCK(wq);
    if (!ok)
        wq_item_free(item);
    return ok ? 0 : -1;
}

/* The in-memory queue is a cache of the journal: keep up to half the pending cap staged. */
static void wq_fill_window(fl_workqueue_t *wq) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t batch[8];
    unsigned room, n = 0;

    if (!j || j->count(j->ctx, FL_WQ_ST_PENDING, NULL) == 0)
        return;
    for (;;) {
        WQ_LOCK(wq);
        room = wq->st.durable_inmem < FL_WQ_PENDING_MAX / 2u &&
                       (unsigned)pq_count(&wq->st.pq) + wq->st.timer_count < FL_WQ_PENDING_MAX - FL_WQ_PROTECTED_RESERVE
                   ? FL_WQ_PENDING_MAX / 2u - wq->st.durable_inmem
                   : 0;
        WQ_UNLOCK(wq);
        if (room == 0)
            return;
        if (room > 8)
            room = 8;
        if (j->scan(j->ctx, FL_WQ_ST_PENDING, NULL, NULL, room, batch, &n) != FL_RESULT_OK || n == 0)
            return;
        for (unsigned i = 0; i < n; i++) {
            if (j->mark(j->ctx, batch[i].id, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) != FL_RESULT_OK)
                continue; /* another thread staged it first */
            if (wq_stage(wq, &batch[i]) != 0) {
                (void)j->mark(j->ctx, batch[i].id, FL_WQ_ST_QUEUED, FL_WQ_ST_PENDING);
                return;
            }
        }
    }
}

static void wq_run_durable(wq_item_t *item) {
    fl_workqueue_t *wq = item->owner;
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t r;
    int h;

    if (j->load(j->ctx, item->job_id, &r) == FL_RESULT_OK && r.state == FL_WQ_ST_QUEUED) {
        h = wq_handler_index(r.tag);
        if (r.deadline && wq_now_sec() > r.deadline) {
            (void)j->mark(j->ctx, r.id, FL_WQ_ST_QUEUED, FL_WQ_ST_EXPIRED); /* stale: never run silently */
        } else if (h < 0) {
            (void)j->mark(j->ctx, r.id, FL_WQ_ST_QUEUED, FL_WQ_ST_FAILED);
        } else if (j->mark(j->ctx, r.id, FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING) == FL_RESULT_OK) {
            /* RUNNING is written before the handler: a cut-off run is found at recovery */
            fl_result_t rc = s_handlers[h].fn(r.payload, r.payload_len, r.idem_key);
            fl_wq_state_t next = rc == FL_RESULT_OK ? FL_WQ_ST_DONE
                                 : r.attempts + 1u < r.max_attempts ? FL_WQ_ST_PENDING
                                                                     : FL_WQ_ST_FAILED;

            (void)j->mark(j->ctx, r.id, FL_WQ_ST_RUNNING, next);
        }
    }
    WQ_LOCK(wq);
    if (wq->st.durable_inmem)
        wq->st.durable_inmem--;
    WQ_UNLOCK(wq);
    wq_item_free(item);
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
    if (owner && owner[0]) {
        size_t len = 0;

        while (owner[len] && len < FL_HERITAGE_KEY_ID_MAX)
            len++;
        if (len >= FL_HERITAGE_KEY_ID_MAX)
            return FL_RESULT_INVAL;
    }
    h = wq_handler_index(tag);
    if (h < 0)
        return FL_RESULT_NOENT; /* nothing could run it after a restart */
    if (wq->st.shutdown)
        return FL_RESULT_BUSY;
    if (j->count(j->ctx, FL_WQ_ST_PENDING, NULL) >= FL_WQ_JOURNAL_MAX_PENDING)
        return FL_RESULT_BUSY; /* bounded backlog: a clear "retry later" */

    {
        unsigned char *b = (unsigned char *)&r;

        for (size_t k = 0; k < sizeof(r); k++)
            b[k] = 0;
    }
    wq_copy(r.idem_key, idem_key, 16);
    r.tag = tag;
    r.layer = layer;
    r.recover = s_handlers[h].recover;
    if (owner)
        for (size_t k = 0; owner[k]; k++)
            r.owner[k] = owner[k];
    r.state = FL_WQ_ST_PENDING;
    r.max_attempts = FL_WQ_MAX_ATTEMPTS;
    r.accepted_at = wq_now_sec();
    r.deadline = ttl_sec > 0 ? r.accepted_at + ttl_sec : 0;
    r.payload_len = (uint32_t)n;
    if (n)
        wq_copy(r.payload, payload, n);

    rc = j->append(j->ctx, &r, &id, &existing); /* accept and persist FIRST: not durable means not accepted */
    if (rc != FL_RESULT_OK)
        return rc;
    if (!existing)
        wq_fill_window(wq);
    return FL_RESULT_OK;
}

fl_result_t fl_wq_recover(fl_workqueue_t *wq) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t batch[16], cursor;
    unsigned n;
    int have;
    int64_t now = wq_now_sec();

    if (!wq)
        return FL_RESULT_INVAL;
    /* a job cut off mid-run (RUNNING) is retried, or FAILED once its attempts are spent; a staged one
     * (QUEUED) simply goes back to waiting */
    for (int pass = 0; pass < 2; pass++) {
        fl_wq_state_t st = pass == 0 ? FL_WQ_ST_RUNNING : FL_WQ_ST_QUEUED;

        have = 0;
        while (j->scan(j->ctx, st, NULL, have ? &cursor : NULL, 16, batch, &n) == FL_RESULT_OK && n) {
            for (unsigned i = 0; i < n; i++) {
                fl_wq_state_t to = batch[i].attempts >= batch[i].max_attempts && st == FL_WQ_ST_RUNNING
                                       ? FL_WQ_ST_FAILED
                                       : FL_WQ_ST_PENDING;

                (void)j->mark(j->ctx, batch[i].id, st, to);
            }
            wq_copy(&cursor, &batch[n - 1], sizeof(cursor));
            have = 1;
        }
    }
    /* stale work is EXPIRED and RECOVER_ASK work is HELD; the rest stays PENDING for the window */
    have = 0;
    while (j->scan(j->ctx, FL_WQ_ST_PENDING, NULL, have ? &cursor : NULL, 16, batch, &n) == FL_RESULT_OK && n) {
        for (unsigned i = 0; i < n; i++) {
            if (batch[i].deadline && now > batch[i].deadline)
                (void)j->mark(j->ctx, batch[i].id, FL_WQ_ST_PENDING, FL_WQ_ST_EXPIRED);
            else if (batch[i].recover == FL_WQ_RECOVER_ASK)
                (void)j->mark(j->ctx, batch[i].id, FL_WQ_ST_PENDING, FL_WQ_ST_HELD);
        }
        wq_copy(&cursor, &batch[n - 1], sizeof(cursor));
        have = 1;
    }
    wq_fill_window(wq);
    return FL_RESULT_OK;
}

unsigned fl_wq_held_count(const char *owner) {
    const fl_wq_journal_ops_t *j = s_journal;

    return j->count(j->ctx, FL_WQ_ST_HELD, owner ? owner : "");
}

fl_result_t fl_wq_resolve_held(const char *owner, int run_now) {
    const fl_wq_journal_ops_t *j = s_journal;
    fl_wq_journal_record_t batch[16], cursor;
    unsigned n;
    int have = 0;

    if (!owner)
        return FL_RESULT_INVAL;
    while (j->scan(j->ctx, FL_WQ_ST_HELD, owner, have ? &cursor : NULL, 16, batch, &n) == FL_RESULT_OK && n) {
        for (unsigned i = 0; i < n; i++)
            (void)j->mark(j->ctx, batch[i].id, FL_WQ_ST_HELD, run_now ? FL_WQ_ST_PENDING : FL_WQ_ST_SKIPPED);
        wq_copy(&cursor, &batch[n - 1], sizeof(cursor));
        have = 1;
    }
    if (run_now)
        wq_fill_window(fl_wq_default());
    return FL_RESULT_OK;
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

    wq_fill_window(wq); /* stage durable work from the journal before the poll starts */
    WQ_LOCK(wq);
#ifdef FL_WQ_DEBUG_REENTRANCY
    assert(wq->st.poll_depth == 0 &&
           "fl_wq_poll: one poller per queue — no nested poll before P9-3 SMP");
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
        /* else: no P1-7 on this path — fall through to best-effort single poll */
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
