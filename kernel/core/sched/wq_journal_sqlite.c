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

#define COLS "id, idem_key, tag, layer, recover, owner, state, attempts, max_attempts, accepted_at, deadline, payload"

static void row_to_record(sqlite3_stmt *st, fl_wq_journal_record_t *r) {
    const void *b;
    int len;
    const unsigned char *o;

    memset(r, 0, sizeof(*r));
    r->id = (uint64_t)sqlite3_column_int64(st, 0);
    b = sqlite3_column_blob(st, 1);
    len = sqlite3_column_bytes(st, 1);
    if (b && len == 16)
        memcpy(r->idem_key, b, 16);
    r->tag = (uint32_t)sqlite3_column_int64(st, 2);
    r->layer = sqlite3_column_int(st, 3);
    r->recover = (uint8_t)sqlite3_column_int(st, 4);
    o = sqlite3_column_text(st, 5);
    if (o) {
        size_t n = strlen((const char *)o);

        if (n >= sizeof(r->owner))
            n = sizeof(r->owner) - 1;
        memcpy(r->owner, o, n);
    }
    r->state = (fl_wq_state_t)sqlite3_column_int(st, 6);
    r->attempts = (uint32_t)sqlite3_column_int(st, 7);
    r->max_attempts = (uint32_t)sqlite3_column_int(st, 8);
    r->accepted_at = sqlite3_column_int64(st, 9);
    r->deadline = sqlite3_column_int64(st, 10);
    b = sqlite3_column_blob(st, 11);
    len = sqlite3_column_bytes(st, 11);
    if (b && len > 0 && len <= (int)FL_WQ_PAYLOAD_MAX) {
        memcpy(r->payload, b, (size_t)len);
        r->payload_len = (uint32_t)len;
    }
}

static fl_result_t wqj_load_locked(wqj_t *w, uint64_t id, fl_wq_journal_record_t *out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (sqlite3_prepare_v2(w->db, "SELECT " COLS " FROM wq_jobs WHERE id = ?", -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        row_to_record(st, out);
        rc = FL_RESULT_OK;
    }
    sqlite3_finalize(st);
    return rc;
}

static fl_result_t wqj_append(void *ctx, const fl_wq_journal_record_t *r, uint64_t *id_out, int *existing) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db, "SELECT id FROM wq_jobs WHERE idem_key = ?", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, r->idem_key, 16, SQLITE_STATIC);
        if (sqlite3_step(st) == SQLITE_ROW) {
            *id_out = (uint64_t)sqlite3_column_int64(st, 0);
            *existing = 1;
            sqlite3_finalize(st);
            pthread_mutex_unlock(&w->mu);
            return FL_RESULT_OK;
        }
        sqlite3_finalize(st);
    }
    st = NULL;
    if (sqlite3_prepare_v2(w->db,
                           "INSERT INTO wq_jobs (idem_key, tag, layer, recover, owner, state, attempts, max_attempts, "
                           "accepted_at, deadline, payload) VALUES (?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?)",
                           -1, &st, NULL) == SQLITE_OK) {
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
        if (sqlite3_step(st) == SQLITE_DONE) { /* autocommit, synchronous = FULL: on disk before this returns */
            *id_out = (uint64_t)sqlite3_last_insert_rowid(w->db);
            *existing = 0;
            rc = FL_RESULT_OK;
        }
        sqlite3_finalize(st);
    }
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

static fl_result_t wqj_scan(void *ctx, fl_wq_state_t state, const char *owner, const fl_wq_journal_record_t *after,
                            unsigned max, fl_wq_journal_record_t *out, unsigned *n) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    unsigned got = 0;

    *n = 0;
    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db,
                           "SELECT " COLS " FROM wq_jobs WHERE state = ?1 AND (?2 IS NULL OR owner = ?2) "
                           "AND (layer > ?3 OR (layer = ?3 AND id > ?4)) ORDER BY layer, id LIMIT ?5",
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
    while (got < max && sqlite3_step(st) == SQLITE_ROW)
        row_to_record(st, &out[got++]);
    sqlite3_finalize(st);
    pthread_mutex_unlock(&w->mu);
    *n = got;
    return FL_RESULT_OK;
}

static unsigned wqj_count(void *ctx, fl_wq_state_t state, const char *owner) {
    wqj_t *w = (wqj_t *)ctx;
    sqlite3_stmt *st = NULL;
    unsigned c = 0;

    pthread_mutex_lock(&w->mu);
    if (sqlite3_prepare_v2(w->db, "SELECT COUNT(*) FROM wq_jobs WHERE state = ?1 AND (?2 IS NULL OR owner = ?2)", -1,
                           &st, NULL) == SQLITE_OK) {
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

fl_result_t fl_wq_journal_sqlite_open(const char *path, fl_wq_journal_ops_t *ops) {
    static const char schema[] =
        "PRAGMA journal_mode = WAL;"
        "PRAGMA synchronous = FULL;"
        "CREATE TABLE IF NOT EXISTS wq_jobs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, idem_key BLOB NOT NULL UNIQUE, tag INTEGER NOT NULL, "
        "layer INTEGER NOT NULL, recover INTEGER NOT NULL, owner TEXT NOT NULL DEFAULT '', "
        "state INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, max_attempts INTEGER NOT NULL, "
        "accepted_at INTEGER NOT NULL, deadline INTEGER NOT NULL DEFAULT 0, payload BLOB);"
        "CREATE INDEX IF NOT EXISTS wq_jobs_state ON wq_jobs (state, layer, id);";
    wqj_t *w;

    if (!path || !ops)
        return FL_RESULT_INVAL;
    w = (wqj_t *)calloc(1, sizeof(*w));
    if (!w)
        return FL_RESULT_NOMEM;
    if (sqlite3_open(path, &w->db) != SQLITE_OK || fl_sqlite_exec(w->db, "wq_journal", schema) != FL_RESULT_OK) {
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
