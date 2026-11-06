#include "wq_journal_sqlite.h"

#include "fl/sqlite_schema.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sqlite3 *db;
    pthread_mutex_t mu;
} wqj_t;

#define COLS "id, idem_key, tag, layer, recover, owner, state, attempts, max_attempts, accepted_at, deadline, payload, executor"

static void row_owner(sqlite3_stmt *st, fl_wq_journal_record_t *r) {
    const unsigned char *o = sqlite3_column_text(st, 5);
    size_t n;

    if (!o)
        return;
    n = strlen((const char *)o);
    if (n >= sizeof(r->owner))
        n = sizeof(r->owner) - 1;
    memcpy(r->owner, o, n);
}

static void row_payload(sqlite3_stmt *st, fl_wq_journal_record_t *r) {
    const void *b = sqlite3_column_blob(st, 11);
    int len = sqlite3_column_bytes(st, 11);

    if (b && len > 0 && len <= (int)FL_WQ_PAYLOAD_MAX) {
        memcpy(r->payload, b, (size_t)len);
        r->payload_len = (uint32_t)len;
    }
}

static void row_to_record(sqlite3_stmt *st, fl_wq_journal_record_t *r) {
    const void *b;

    memset(r, 0, sizeof(*r));
    r->id = (uint64_t)sqlite3_column_int64(st, 0);
    b = sqlite3_column_blob(st, 1);
    if (b && sqlite3_column_bytes(st, 1) == 16)
        memcpy(r->idem_key, b, 16);
    r->tag = (uint32_t)sqlite3_column_int64(st, 2);
    r->layer = sqlite3_column_int(st, 3);
    r->recover = (uint8_t)sqlite3_column_int(st, 4);
    row_owner(st, r);
    r->state = (fl_wq_state_t)sqlite3_column_int(st, 6);
    r->attempts = (uint32_t)sqlite3_column_int(st, 7);
    r->max_attempts = (uint32_t)sqlite3_column_int(st, 8);
    r->accepted_at = sqlite3_column_int64(st, 9);
    r->deadline = sqlite3_column_int64(st, 10);
    row_payload(st, r);
    r->executor = (uint32_t)sqlite3_column_int64(st, 12);
}

/* ANY is bound as -1: one statement serves a filtered and an unfiltered scan. */
static sqlite3_int64 executor_bind(uint32_t executor) {
    return executor == FL_WQ_EXECUTOR_ANY ? -1 : (sqlite3_int64)executor;
}

static fl_result_t wqj_load_locked(wqj_t *w, uint64_t id, fl_wq_journal_record_t *out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc;
    int step;

    if (sqlite3_prepare_v2(w->db, "SELECT " COLS " FROM wq_jobs WHERE id = ?", -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    step = sqlite3_step(st);
    if (step == SQLITE_ROW) {
        row_to_record(st, out);
        rc = FL_RESULT_OK;
    } else if (step == SQLITE_DONE) {
        rc = FL_RESULT_NOENT;
    } else {
        rc = FL_RESULT_ERR; /* a step error is not absence */
    }
    sqlite3_finalize(st);
    return rc;
}

/* 1 found, 0 absent, -1 the lookup itself failed (an absent key must never be assumed). */
static int wqj_find_idem_locked(wqj_t *w, const uint8_t key[16], uint64_t *id_out) {
    sqlite3_stmt *st = NULL;
    int step, found;

    if (sqlite3_prepare_v2(w->db, "SELECT id FROM wq_jobs WHERE idem_key = ?", -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_blob(st, 1, key, 16, SQLITE_STATIC);
    step = sqlite3_step(st);
    found = step == SQLITE_ROW ? 1 : step == SQLITE_DONE ? 0 : -1;
    if (found == 1)
        *id_out = (uint64_t)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return found;
}

/* PENDING, QUEUED, RUNNING and HELD: the backlog bound. Returns -1 when the count fails. */
static long wqj_outstanding_locked(wqj_t *w) {
    sqlite3_stmt *st = NULL;
    long c = -1;

    if (sqlite3_prepare_v2(w->db, "SELECT COUNT(*) FROM wq_jobs WHERE state IN (?1, ?2, ?3, ?4)", -1, &st, NULL) !=
        SQLITE_OK)
        return -1;
    sqlite3_bind_int(st, 1, FL_WQ_ST_PENDING);
    sqlite3_bind_int(st, 2, FL_WQ_ST_QUEUED);
    sqlite3_bind_int(st, 3, FL_WQ_ST_RUNNING);
    sqlite3_bind_int(st, 4, FL_WQ_ST_HELD);
    if (sqlite3_step(st) == SQLITE_ROW)
        c = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return c;
}

static fl_result_t wqj_insert_locked(wqj_t *w, const fl_wq_journal_record_t *r, uint64_t *id_out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (sqlite3_prepare_v2(w->db,
                           "INSERT INTO wq_jobs (idem_key, tag, layer, recover, owner, state, attempts, max_attempts, "
                           "accepted_at, deadline, payload, executor) VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?)",
                           -1, &st, NULL) != SQLITE_OK)
        return rc;
    sqlite3_bind_blob(st, 1, r->idem_key, 16, SQLITE_STATIC);
    sqlite3_bind_int64(st, 2, r->tag);
    sqlite3_bind_int(st, 3, r->layer);
    sqlite3_bind_int(st, 4, r->recover);
    sqlite3_bind_text(st, 5, r->owner, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 6, FL_WQ_ST_PENDING);
    sqlite3_bind_int(st, 7, (int)r->max_attempts);
    sqlite3_bind_int64(st, 8, r->accepted_at);
    sqlite3_bind_int64(st, 9, r->deadline);
    sqlite3_bind_blob(st, 10, r->payload, (int)r->payload_len, SQLITE_STATIC);
    sqlite3_bind_int64(st, 11, (sqlite3_int64)r->executor);
    if (sqlite3_step(st) == SQLITE_DONE) {
        *id_out = (uint64_t)sqlite3_last_insert_rowid(w->db);
        rc = FL_RESULT_OK;
    }
    sqlite3_finalize(st);
    return rc;
}

/* The lookup, the bound and the insert, under the caller's open transaction. */
static fl_result_t wqj_admit_locked(wqj_t *w, const fl_wq_journal_record_t *r, unsigned max_outstanding,
                                    uint64_t *id_out, int *existing) {
    int found = wqj_find_idem_locked(w, r->idem_key, id_out);
    long outstanding;

    if (found < 0)
        return FL_RESULT_ERR;
    *existing = found;
    if (found)
        return FL_RESULT_OK; /* a repeat is answered before the bound is applied */
    outstanding = wqj_outstanding_locked(w);
    if (outstanding < 0)
        return FL_RESULT_ERR;
    if ((unsigned long)outstanding >= max_outstanding)
        return FL_RESULT_BUSY;
    return wqj_insert_locked(w, r, id_out);
}

static fl_result_t wqj_append(void *ctx, const fl_wq_journal_record_t *r, unsigned max_outstanding,
                              uint64_t *id_out, int *existing) {
    wqj_t *w = (wqj_t *)ctx;
    fl_result_t rc;

    pthread_mutex_lock(&w->mu);
    if (fl_sqlite_exec(w->db, "wq_journal", "BEGIN IMMEDIATE") != FL_RESULT_OK) {
        pthread_mutex_unlock(&w->mu);
        return FL_RESULT_ERR;
    }
    rc = wqj_admit_locked(w, r, max_outstanding, id_out, existing);
    if (rc == FL_RESULT_OK && fl_sqlite_exec(w->db, "wq_journal", "COMMIT") != FL_RESULT_OK)
        rc = FL_RESULT_ERR;
    if (rc != FL_RESULT_OK)
        (void)fl_sqlite_exec(w->db, "wq_journal", "ROLLBACK");
    pthread_mutex_unlock(&w->mu);
    return rc;
}

static fl_result_t wqj_mark(void *ctx, uint64_t id, fl_wq_state_t from, fl_wq_state_t to) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db,
                           "UPDATE wq_jobs SET state = ?, attempts = attempts + ? WHERE id = ? AND state = ?", -1, &st,
                           NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, to);
        sqlite3_bind_int(st, 2, to == FL_WQ_ST_RUNNING ? 1 : 0);
        sqlite3_bind_int64(st, 3, (sqlite3_int64)id);
        sqlite3_bind_int(st, 4, from);
        if (sqlite3_step(st) == SQLITE_DONE)
            rc = sqlite3_changes(w->db) == 1 ? FL_RESULT_OK : FL_RESULT_BUSY;
        sqlite3_finalize(st);
    }
    pthread_mutex_unlock(&w->mu);
    return rc;
}

static fl_result_t wqj_load(void *ctx, uint64_t id, fl_wq_journal_record_t *out) {
    wqj_t *w = (wqj_t *)ctx;
    fl_result_t rc;

    pthread_mutex_lock(&w->mu);
    rc = wqj_load_locked(w, id, out);
    pthread_mutex_unlock(&w->mu);
    return rc;
}

static fl_result_t wqj_scan(void *ctx, fl_wq_state_t state, const char *owner, uint32_t executor,
                            const fl_wq_journal_record_t *after, unsigned max, fl_wq_journal_record_t *out, unsigned *n) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    unsigned got = 0;

    *n = 0;
    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db,
                           "SELECT " COLS " FROM wq_jobs WHERE state = ?1 AND (?2 IS NULL OR owner = ?2) "
                           "AND (layer > ?3 OR (layer = ?3 AND id > ?4)) AND (?6 < 0 OR executor = ?6) "
                           "ORDER BY layer, id LIMIT ?5",
                           -1, &st, NULL) != SQLITE_OK) {
        pthread_mutex_unlock(&w->mu);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int(st, 1, state);
    if (owner)
        sqlite3_bind_text(st, 2, owner, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 2);
    sqlite3_bind_int(st, 3, after ? after->layer : -1);
    sqlite3_bind_int64(st, 4, after ? (sqlite3_int64)after->id : 0);
    sqlite3_bind_int(st, 5, (int)max);
    sqlite3_bind_int64(st, 6, executor_bind(executor));
    for (;;) {
        int step = sqlite3_step(st);

        if (step == SQLITE_ROW) {
            if (got < max)
                row_to_record(st, &out[got++]);
            continue;
        }
        sqlite3_finalize(st);
        pthread_mutex_unlock(&w->mu);
        *n = got;
        /* DONE ends a successful page; any other code is a read error (do not report OK). */
        return step == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
    }
}

static unsigned wqj_count(void *ctx, fl_wq_state_t state, const char *owner, uint32_t executor) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    unsigned c = 0;

    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db, "SELECT COUNT(*) FROM wq_jobs WHERE state = ?1 AND (?2 IS NULL OR owner = ?2) "
                           "AND (?3 < 0 OR executor = ?3)",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 3, executor_bind(executor));
        sqlite3_bind_int(st, 1, state);
        if (owner)
            sqlite3_bind_text(st, 2, owner, -1, SQLITE_STATIC);
        else
            sqlite3_bind_null(st, 2);
        if (sqlite3_step(st) == SQLITE_ROW)
            c = (unsigned)sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    pthread_mutex_unlock(&w->mu);
    return c;
}

static const char wqj_schema[] =
    "PRAGMA journal_mode = WAL;"
    "PRAGMA synchronous = FULL;"
    "CREATE TABLE IF NOT EXISTS wq_jobs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, idem_key BLOB NOT NULL UNIQUE, tag INTEGER NOT NULL, "
    "layer INTEGER NOT NULL, recover INTEGER NOT NULL, owner TEXT NOT NULL DEFAULT '', "
    "state INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, max_attempts INTEGER NOT NULL, "
    "accepted_at INTEGER NOT NULL, deadline INTEGER NOT NULL DEFAULT 0, payload BLOB, "
    "executor INTEGER NOT NULL DEFAULT 1);"
    "CREATE INDEX IF NOT EXISTS wq_jobs_state ON wq_jobs (state, layer, id);";

/* A store written before executors held no such column: its rows get the default executor (1 =
 * FL_WQ_EXECUTOR_DEFAULT), the queue every record belonged to then. */
static const fl_sqlite_column_t wqj_columns[] = {{"executor", "INTEGER NOT NULL DEFAULT 1"}};
_Static_assert(FL_WQ_EXECUTOR_DEFAULT == 1u, "the executor column default in wqj_columns");

fl_result_t fl_wq_journal_sqlite_open(const char *path, fl_wq_journal_ops_t *ops) {
    wqj_t *w;

    if (!path || !ops)
        return FL_RESULT_INVAL;
    w = (wqj_t *)calloc(1, sizeof(*w));
    if (!w)
        return FL_RESULT_NOMEM;
    if (sqlite3_open(path, &w->db) != SQLITE_OK || fl_sqlite_exec(w->db, "wq_journal", wqj_schema) != FL_RESULT_OK ||
        fl_sqlite_ensure_columns(w->db, "wq_journal", "wq_jobs", wqj_columns, 1) != FL_RESULT_OK) {
        if (w->db)
            sqlite3_close(w->db);
        free(w);
        return FL_RESULT_ERR;
    }
    pthread_mutex_init(&w->mu, NULL);
    ops->ctx = w;
    ops->append = wqj_append;
    ops->mark = wqj_mark;
    ops->load = wqj_load;
    ops->scan = wqj_scan;
    ops->count = wqj_count;
    return FL_RESULT_OK;
}

void fl_wq_journal_sqlite_close(fl_wq_journal_ops_t *ops) {
    wqj_t *w;

    if (!ops || !ops->ctx)
        return;
    w = (wqj_t *)ops->ctx;
    sqlite3_close(w->db);
    pthread_mutex_destroy(&w->mu);
    free(w);
    ops->ctx = NULL;
}
