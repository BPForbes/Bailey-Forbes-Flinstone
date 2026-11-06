/* The workqueue's flushed journal (FL-25) on SQLite: the same scenarios as the memory journal, with a
 * real restart (close and reopen the file) in between. Hosted only. */
#include "wq_journal_sqlite.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static unsigned s_ran;
static fl_result_t handler(const void *p, size_t n, const uint8_t idem[16]) {
    (void)p; (void)n; (void)idem;
    s_ran++;
    return FL_RESULT_OK;
}

#define TAG_AUTO 0x7101u
#define TAG_ASK 0x7102u

/* After the reboot: recovery, held work, a lost compare-and-set, and declining the held occurrence. */
static int recover_and_resolve_held(fl_wq_journal_ops_t *j, fl_workqueue_t *b) {
    uint8_t k4[16] = {4};
    fl_wq_journal_record_t r;

    ASSERT(j->count(j->ctx, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 3 && s_ran == 0);
    ASSERT(fl_wq_init(b) == FL_RESULT_OK && fl_wq_set_executor(b, FL_WQ_EXECUTOR_DEFAULT) == FL_RESULT_OK);
    ASSERT(fl_wq_recover(b) == FL_RESULT_OK);
    ASSERT(fl_wq_held_count("Root.Bob") == 1); /* the user-visible one waits */
    ASSERT(fl_wq_poll_at(b, 8, 0) == 2 && s_ran == 2);
    ASSERT(j->count(j->ctx, FL_WQ_ST_DONE, NULL, FL_WQ_EXECUTOR_ANY) == 2);
    ASSERT(j->load(j->ctx, 1, &r) == FL_RESULT_OK);
    ASSERT(r.payload_len == 3 && r.payload[2] == 9 && strcmp(r.owner, "Root.Bob") == 0 && r.attempts == 1);
    /* a stale compare-and-set loses cleanly */
    ASSERT(j->mark(j->ctx, 1, FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_BUSY);
    /* declining the held occurrence drops only it; a later job with a new key still runs */
    ASSERT(fl_wq_resolve_held("Root.Bob", 0) == FL_RESULT_OK);
    ASSERT(j->count(j->ctx, FL_WQ_ST_SKIPPED, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    ASSERT(fl_wq_enqueue_durable(b, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, k4, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_poll_at(b, 8, 0) == 1 && s_ran == 3);
    return 0;
}

static int test_survives_a_restart(const char *path) {
    fl_wq_journal_ops_t j;
    fl_workqueue_storage_t sa, sb;
    fl_workqueue_t *a = (fl_workqueue_t *)&sa, *b = (fl_workqueue_t *)&sb;
    uint8_t k1[16] = {1}, k2[16] = {2}, k3[16] = {3};
    uint8_t pl[3] = {7, 8, 9};

    s_ran = 0;
    ASSERT(fl_wq_register_handler(TAG_AUTO, handler, FL_WQ_RECOVER_AUTO) == FL_RESULT_OK);
    ASSERT(fl_wq_register_handler(TAG_ASK, handler, FL_WQ_RECOVER_ASK) == FL_RESULT_OK);
    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    ASSERT(fl_wq_set_journal(&j) == fl_wq_journal_mem());
    ASSERT(fl_wq_init(a) == FL_RESULT_OK && fl_wq_set_executor(a, FL_WQ_EXECUTOR_DEFAULT) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, "Root.Bob", pl, sizeof pl, k1, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, NULL, NULL, 0, k2, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_ASK, FL_WQ_LAYER_BACKGROUND, "Root.Bob", NULL, 0, k3, 0) == FL_RESULT_OK);
    ASSERT(fl_wq_enqueue_durable(a, TAG_AUTO, FL_WQ_LAYER_NORMAL, "Root.Bob", pl, sizeof pl, k1, 0) == FL_RESULT_OK); /* repeat */
    ASSERT(j.count(j.ctx, FL_WQ_ST_QUEUED, NULL, FL_WQ_EXECUTOR_ANY) == 3);
    fl_wq_shutdown(a); /* crash: memory gone */
    fl_wq_journal_sqlite_close(&j);

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK); /* reboot: the file is all that is left */
    (void)fl_wq_set_journal(&j);
    ASSERT(recover_and_resolve_held(&j, b) == 0);
    fl_wq_shutdown(b);
    (void)fl_wq_set_journal(NULL);
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

static void fill_record(fl_wq_journal_record_t *r, unsigned key_no, uint32_t executor) {
    memset(r, 0, sizeof *r);
    r->executor = executor;
    r->tag = TAG_AUTO;
    r->layer = FL_WQ_LAYER_NORMAL;
    r->max_attempts = FL_WQ_MAX_ATTEMPTS;
    r->idem_key[0] = (uint8_t)key_no;
    r->idem_key[1] = (uint8_t)(key_no >> 8);
}

/* One transaction decides: the bound counts PENDING, QUEUED, RUNNING and HELD, and a repeat is answered first. */
static int admission_counts_every_nonterminal_state(const fl_wq_journal_ops_t *j) {
    fl_wq_journal_record_t r;
    uint64_t id[4], again;
    int ex;

    for (unsigned i = 0; i < 4; i++) {
        fill_record(&r, 100 + i, FL_WQ_EXECUTOR_DEFAULT);
        ASSERT(j->append(j->ctx, &r, 4, &id[i], &ex) == FL_RESULT_OK && !ex);
    }
    ASSERT(j->mark(j->ctx, id[1], FL_WQ_ST_PENDING, FL_WQ_ST_QUEUED) == FL_RESULT_OK);
    ASSERT(j->mark(j->ctx, id[1], FL_WQ_ST_QUEUED, FL_WQ_ST_RUNNING) == FL_RESULT_OK);
    ASSERT(j->mark(j->ctx, id[2], FL_WQ_ST_PENDING, FL_WQ_ST_HELD) == FL_RESULT_OK);
    fill_record(&r, 200, FL_WQ_EXECUTOR_DEFAULT);
    ASSERT(j->append(j->ctx, &r, 4, &again, &ex) == FL_RESULT_BUSY); /* nothing was written by the refusal */
    ASSERT(j->count(j->ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 2);
    fill_record(&r, 100, FL_WQ_EXECUTOR_DEFAULT);
    ASSERT(j->append(j->ctx, &r, 4, &again, &ex) == FL_RESULT_OK && ex && again == id[0]); /* the repeat wins */
    ASSERT(j->mark(j->ctx, id[3], FL_WQ_ST_PENDING, FL_WQ_ST_EXPIRED) == FL_RESULT_OK);
    fill_record(&r, 200, FL_WQ_EXECUTOR_DEFAULT);
    ASSERT(j->append(j->ctx, &r, 4, &again, &ex) == FL_RESULT_OK && !ex);
    return 0;
}

/* The executor survives a restart, filters scan and count, and an old store gets the default one. */
static int executor_is_stored_and_filters(const char *path) {
    fl_wq_journal_ops_t j;
    fl_wq_journal_record_t r, out[4];
    uint64_t id;
    unsigned n;
    int ex;

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    fill_record(&r, 300, 5);
    ASSERT(j.append(j.ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK && !ex);
    fl_wq_journal_sqlite_close(&j);
    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    ASSERT(j.load(j.ctx, id, &r) == FL_RESULT_OK && r.executor == 5);
    ASSERT(j.scan(j.ctx, FL_WQ_ST_PENDING, NULL, 5, NULL, 4, out, &n) == FL_RESULT_OK && n == 1 && out[0].id == id);
    ASSERT(j.scan(j.ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_DEFAULT, NULL, 4, out, &n) == FL_RESULT_OK && n == 0);
    ASSERT(j.count(j.ctx, FL_WQ_ST_PENDING, NULL, 5) == 1 && j.count(j.ctx, FL_WQ_ST_PENDING, NULL, 6) == 0);
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

static int admission_in_a_fresh_file(const char *path) {
    fl_wq_journal_ops_t j;
    int rc;

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    rc = admission_counts_every_nonterminal_state(&j);
    fl_wq_journal_sqlite_close(&j);
    return rc;
}

static int old_store_migrates_to_the_default_executor(const char *path) {
    fl_wq_journal_ops_t j;
    fl_wq_journal_record_t r;
    sqlite3 *db;

    ASSERT(sqlite3_open(path, &db) == SQLITE_OK);
    ASSERT(sqlite3_exec(db,
                        "CREATE TABLE wq_jobs (id INTEGER PRIMARY KEY AUTOINCREMENT, idem_key BLOB NOT NULL UNIQUE, "
                        "tag INTEGER NOT NULL, layer INTEGER NOT NULL, recover INTEGER NOT NULL, "
                        "owner TEXT NOT NULL DEFAULT '', state INTEGER NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, "
                        "max_attempts INTEGER NOT NULL, accepted_at INTEGER NOT NULL, "
                        "deadline INTEGER NOT NULL DEFAULT 0, payload BLOB);"
                        "INSERT INTO wq_jobs (idem_key, tag, layer, recover, state, max_attempts, accepted_at) "
                        "VALUES (x'00112233445566778899aabbccddeeff', 28673, 1, 0, 1, 3, 1);",
                        NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(db);
    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    ASSERT(j.load(j.ctx, 1, &r) == FL_RESULT_OK && r.executor == FL_WQ_EXECUTOR_DEFAULT);
    ASSERT(j.count(j.ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_DEFAULT) == 1);
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

/* Another connection holds the write lock: BEGIN fails, nothing is written, and the append works once it lets go. */
static int append_that_cannot_begin_writes_nothing(const char *path) {
    fl_wq_journal_ops_t j;
    fl_wq_journal_record_t r;
    sqlite3 *other;
    uint64_t id;
    int ex;

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    ASSERT(sqlite3_open(path, &other) == SQLITE_OK);
    ASSERT(sqlite3_exec(other, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK);
    fill_record(&r, 400, FL_WQ_EXECUTOR_DEFAULT);
    ASSERT(j.append(j.ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_ERR);
    ASSERT(sqlite3_exec(other, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(other);
    ASSERT(j.count(j.ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 0);
    ASSERT(j.append(j.ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK && !ex); /* no stuck transaction */
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

/* COMMIT refused after a successful insert: the transaction rolls back and a later append works. */
static int append_that_cannot_commit_writes_nothing(const char *path) {
    fl_wq_journal_ops_t j;
    fl_wq_journal_record_t r;
    uint64_t id;
    int ex;

    ASSERT(fl_wq_journal_sqlite_open(path, &j) == FL_RESULT_OK);
    fill_record(&r, 500, FL_WQ_EXECUTOR_DEFAULT);
    fl_wq_journal_sqlite_fail_next_commits(&j, 1);
    ASSERT(j.append(j.ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_ERR);
    ASSERT(j.count(j.ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 0);
    ASSERT(j.append(j.ctx, &r, FL_WQ_JOURNAL_MAX_PENDING, &id, &ex) == FL_RESULT_OK && !ex);
    ASSERT(j.count(j.ctx, FL_WQ_ST_PENDING, NULL, FL_WQ_EXECUTOR_ANY) == 1);
    fl_wq_journal_sqlite_close(&j);
    return 0;
}

static void remove_db(const char *path) {
    char side[64];

    unlink(path);
    snprintf(side, sizeof side, "%s-wal", path); unlink(side);
    snprintf(side, sizeof side, "%s-shm", path); unlink(side);
}

static int test_fresh_file(int (*fn)(const char *path)) {
    char path[] = "/tmp/fl_wq_journal_XXXXXX";
    int fd = mkstemp(path), rc;

    if (fd < 0)
        return 1;
    close(fd);
    if (fn == old_store_migrates_to_the_default_executor)
        unlink(path); /* the test creates the old-format file itself */
    rc = fn(path);
    remove_db(path);
    return rc;
}

int main(void) {
    char path[] = "/tmp/fl_wq_journal_XXXXXX";
    int fd = mkstemp(path);
    int rc;

    if (fd < 0)
        return 1;
    close(fd);
    rc = test_survives_a_restart(path);
    unlink(path);
    {
        char side[64];
        snprintf(side, sizeof side, "%s-wal", path); unlink(side);
        snprintf(side, sizeof side, "%s-shm", path); unlink(side);
    }
    if (rc || test_fresh_file(executor_is_stored_and_filters) || test_fresh_file(admission_in_a_fresh_file) ||
        test_fresh_file(append_that_cannot_begin_writes_nothing) ||
        test_fresh_file(append_that_cannot_commit_writes_nothing) ||
        test_fresh_file(old_store_migrates_to_the_default_executor))
        return 1;
    if (fl_wq_illegal_moves() != 0) { /* the flushed journal saw only moves the Lean model allows */
        fprintf(stderr, "FAIL: the queue asked the SQLite journal for a move the model forbids\n");
        return 1;
    }
    printf("test_wq_journal_sqlite: all passed\n");
    return 0;
}
