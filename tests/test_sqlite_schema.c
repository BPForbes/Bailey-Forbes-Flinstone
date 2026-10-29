/**
 * test_sqlite_schema.c - fl_sqlite_exec and fl_sqlite_ensure_columns
 * (kernel/include/fl/sqlite_schema.h).
 */
#include "fl/sqlite_schema.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static sqlite3 *open_mem(const char *ddl) {
    sqlite3 *db = NULL;

    if (sqlite3_open(":memory:", &db) != SQLITE_OK)
        return NULL;
    if (ddl && sqlite3_exec(db, ddl, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

static int column_count(sqlite3 *db, const char *table) {
    sqlite3_stmt *st = NULL;
    int n = 0;

    if (sqlite3_prepare_v2(db, "SELECT count(*) FROM pragma_table_info(?1);", -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC);
    if (sqlite3_step(st) == SQLITE_ROW)
        n = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return n;
}

static int scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *st = NULL;
    int v = -1;

    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return v;
}

/* Point stderr at a temp file. stderr_capture_end puts the original fd back. */
static int stderr_capture_begin(FILE **cap, int *saved) {
    int fd;

    fflush(stderr);
    fd = dup(fileno(stderr));
    if (fd < 0)
        return -1;
    *cap = tmpfile();
    if (!*cap) {
        close(fd);
        return -1;
    }
    if (dup2(fileno(*cap), fileno(stderr)) < 0) {
        fclose(*cap);
        close(fd);
        *cap = NULL;
        return -1;
    }
    *saved = fd;
    return 0;
}

static int stderr_capture_end(FILE *cap, int saved, char *buf, size_t buflen) {
    size_t n = 0;
    int rc = 0;

    fflush(stderr);
    if (dup2(saved, fileno(stderr)) < 0)
        rc = -1;
    close(saved);
    if (buflen > 0)
        buf[0] = '\0';
    if (cap) {
        rewind(cap);
        if (buflen > 1) {
            n = fread(buf, 1, buflen - 1, cap);
            buf[n] = '\0';
        }
        fclose(cap);
    }
    return rc;
}

/* The limits the header documents, pinned so a change to either is a test failure. */
_Static_assert(FL_SQLITE_IDENT_MAX == 64u, "identifier limit is documented as 64 bytes");
_Static_assert(FL_SQLITE_DECL_MAX == 128u, "column type limit is documented as 128 bytes");

static const fl_sqlite_column_t kCols[] = {
    {"note", "TEXT NOT NULL DEFAULT ''"},
    {"stamp", "INTEGER NOT NULL DEFAULT 0"},
};

static int test_adds_and_idempotent(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT PRIMARY KEY); INSERT INTO t VALUES('a');");

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_OK);
    ASSERT(column_count(db, "t") == 3);
    /* The migration committed: no savepoint (and so no write lock) is left open. */
    ASSERT(sqlite3_get_autocommit(db));
    ASSERT(scalar(db, "SELECT count(*) FROM t WHERE note='' AND stamp=0;") == 1);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_OK);
    ASSERT(column_count(db, "t") == 3);
    ASSERT(scalar(db, "SELECT count(*) FROM t;") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_all_present_unchanged(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT, note TEXT, stamp INTEGER);");

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_OK);
    /* Declared types untouched: no ALTER ran. */
    ASSERT(scalar(db, "SELECT count(*) FROM pragma_table_info('t') WHERE name='note' AND \"notnull\"=0;") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_mixed_case_existing(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT, NoTe TEXT, STAMP INTEGER);");

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_OK);
    ASSERT(column_count(db, "t") == 3);
    sqlite3_close(db);
    return 0;
}

static int test_missing_table_and_null_args(void) {
    sqlite3 *db = open_mem(NULL);

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "nope", kCols, 2) == FL_RESULT_ERR);
    ASSERT(scalar(db, "SELECT count(*) FROM sqlite_master;") == 0);
    ASSERT(fl_sqlite_ensure_columns(NULL, "test", "t", kCols, 2) == FL_RESULT_INVAL);
    ASSERT(fl_sqlite_ensure_columns(db, "test", NULL, kCols, 2) == FL_RESULT_INVAL);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", NULL, 2) == FL_RESULT_INVAL);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 0) == FL_RESULT_INVAL);
    sqlite3_close(db);
    return 0;
}

static int test_bad_identifiers(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT); CREATE TABLE victim (x TEXT);");
    char longname[FL_SQLITE_IDENT_MAX + 8];
    const char *bad_names[] = {"x; DROP TABLE victim", "a\"b", "", "1abc", "a b", "a-b", longname};
    const char *bad_decls[] = {"TEXT; DROP TABLE victim", "TEXT -- c", "TEXT /* c */", "", "TEXT\n",
                               "TEXT DEFAULT \"x\"", "TEXT%s", "TEXT DEFAULT 'a",
                               "TEXT DEFAULT (1)", "TEXT DEFAULT (load_extension('x'))", "TEXT REFERENCES victim(x)"};
    size_t i;

    ASSERT(db);
    memset(longname, 'a', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    for (i = 0; i < sizeof bad_names / sizeof bad_names[0]; i++) {
        fl_sqlite_column_t c = {bad_names[i], "TEXT NOT NULL DEFAULT ''"};

        ASSERT(fl_sqlite_ensure_columns(db, "test", "t", &c, 1) == FL_RESULT_INVAL);
        ASSERT(fl_sqlite_ensure_columns(db, "test", bad_names[i], kCols, 2) == FL_RESULT_INVAL);
    }
    for (i = 0; i < sizeof bad_decls / sizeof bad_decls[0]; i++) {
        fl_sqlite_column_t c = {"ok", bad_decls[i]};

        ASSERT(fl_sqlite_ensure_columns(db, "test", "t", &c, 1) == FL_RESULT_INVAL);
    }
    {
        /* One bad entry after a good one: nothing is written for either. */
        fl_sqlite_column_t mixed[2] = {{"good", "TEXT NOT NULL DEFAULT ''"}, {"bad;", "TEXT"}};

        ASSERT(fl_sqlite_ensure_columns(db, "test", "t", mixed, 2) == FL_RESULT_INVAL);
    }
    ASSERT(column_count(db, "t") == 1);
    ASSERT(column_count(db, "victim") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_inval_stderr_lines(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT);");
    fl_sqlite_column_t bad_col = {"a b", "TEXT"};
    char buf[256];
    FILE *cap = NULL;
    int saved = -1;
    fl_result_t rc_table;
    fl_result_t rc_col;
    fl_result_t rc_null;

    ASSERT(db);
    ASSERT(stderr_capture_begin(&cap, &saved) == 0);
    rc_table = fl_sqlite_ensure_columns(db, "test", "bad name", kCols, 2);
    rc_col = fl_sqlite_ensure_columns(db, "test", "t", &bad_col, 1);
    ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
    ASSERT(rc_table == FL_RESULT_INVAL);
    ASSERT(rc_col == FL_RESULT_INVAL);
    ASSERT(strcmp(buf, "test: invalid column definition (table name)\n"
                       "test: invalid column definition (column name)\n") == 0);
    ASSERT(stderr_capture_begin(&cap, &saved) == 0);
    rc_null = fl_sqlite_ensure_columns(NULL, "test", "t", kCols, 2);
    ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
    ASSERT(rc_null == FL_RESULT_INVAL);
    ASSERT(strstr(buf, "invalid column definition") == NULL);
    ASSERT(buf[0] == '\0');
    ASSERT(column_count(db, "t") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_ident_bounds(void) {
    char name[FL_SQLITE_IDENT_MAX + 2];

    memset(name, 'a', sizeof name - 1);
    name[FL_SQLITE_IDENT_MAX] = '\0';
    ASSERT(fl_sqlite_priv_ident_ok(name));
    name[FL_SQLITE_IDENT_MAX] = 'a';
    name[FL_SQLITE_IDENT_MAX + 1] = '\0';
    ASSERT(!fl_sqlite_priv_ident_ok(name));
    ASSERT(fl_sqlite_priv_ident_ok("_a9"));
    ASSERT(!fl_sqlite_priv_ident_ok("9a"));
    return 0;
}

static int test_alter_failure_is_error(void) {
    /* NOT NULL without a default cannot be added to a non-empty table: ALTER fails, helper reports ERR. */
    sqlite3 *db = open_mem("CREATE TABLE t (name TEXT); INSERT INTO t VALUES('a');");
    fl_sqlite_column_t c = {"req", "TEXT NOT NULL"};

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", &c, 1) == FL_RESULT_ERR);
    ASSERT(column_count(db, "t") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_atomic_multi_column(void) {
    /* 2nd column cannot be added (NOT NULL, no default, non-empty table): the 1st is rolled back too. */
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT); INSERT INTO t VALUES('x');");
    fl_sqlite_column_t cols[2] = {{"b", "TEXT NOT NULL DEFAULT ''"}, {"c", "TEXT NOT NULL"}};

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", cols, 2) == FL_RESULT_ERR);
    ASSERT(column_count(db, "t") == 1);
    ASSERT(sqlite3_get_autocommit(db));
    /* The same inside the caller's transaction: it stays open and usable. */
    ASSERT(sqlite3_exec(db, "BEGIN; INSERT INTO t VALUES('y');", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", cols, 2) == FL_RESULT_ERR);
    ASSERT(!sqlite3_get_autocommit(db));
    ASSERT(column_count(db, "t") == 1);
    ASSERT(sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(scalar(db, "SELECT count(*) FROM t;") == 2);
    sqlite3_close(db);
    return 0;
}

static int test_repeated_name_refused(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT);");
    fl_sqlite_column_t cols[2] = {{"b", "TEXT"}, {"B", "TEXT"}};

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", cols, 2) == FL_RESULT_INVAL);
    ASSERT(column_count(db, "t") == 1);
    sqlite3_close(db);
    return 0;
}

static int test_too_many_columns(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT);");
    fl_sqlite_column_t cols[33];
    char names[33][8];
    size_t i;

    ASSERT(db);
    for (i = 0; i < 33; i++) {
        snprintf(names[i], sizeof names[i], "c%zu", i);
        cols[i].name = names[i];
        cols[i].decl = "TEXT";
    }
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", cols, 33) == FL_RESULT_INVAL);
    ASSERT(column_count(db, "t") == 1);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", cols, 32) == FL_RESULT_OK);
    ASSERT(column_count(db, "t") == 33);
    sqlite3_close(db);
    return 0;
}

static int interrupt_always(void *unused) {
    (void)unused;
    return 1;
}

static int test_probe_step_error(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT);");

    ASSERT(db);
    sqlite3_progress_handler(db, 1, interrupt_always, NULL);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_ERR);
    sqlite3_progress_handler(db, 0, NULL, NULL);
    ASSERT(column_count(db, "t") == 1);
    sqlite3_close(db);
    return 0;
}

/* Progress-handler budget: interrupt every step from the at'th on (sticky) or only the at'th (once). */
typedef struct {
    int n;
    int at;
    int sticky;
    int fail_undo; /* also interrupt every ROLLBACK TO (the helper's undo) */
    int in_undo;
} interrupt_plan_t;

static int interrupt_planned(void *arg) {
    interrupt_plan_t *p = (interrupt_plan_t *)arg;

    p->n++;
    if (p->fail_undo && p->in_undo)
        return 1;
    return p->sticky ? p->n >= p->at : p->n == p->at;
}

/* Marks the statement now starting, so interrupt_planned can fail the undo alone. */
static int trace_statement(unsigned type, void *arg, void *stmt, void *unused) {
    interrupt_plan_t *p = (interrupt_plan_t *)arg;
    const char *sql = sqlite3_sql((sqlite3_stmt *)stmt);

    (void)type;
    (void)unused;
    p->in_undo = sql && strncmp(sql, "ROLLBACK TO", strlen("ROLLBACK TO")) == 0;
    return 0;
}

/* A step error anywhere, the probe after it read rows included, is reported as that error: never a
 * missing table, never silent. Every interrupt point is tried until one lets the call finish. */
static int test_probe_error_after_rows(void) {
    int at;
    int finished = 0;

    for (at = 1; at < 2000 && !finished; at++) {
        sqlite3 *db = open_mem("CREATE TABLE t (a TEXT, b TEXT, c TEXT);");
        interrupt_plan_t plan = {0, at, 1, 0, 0};
        char buf[512];
        FILE *cap = NULL;
        int saved = -1;
        fl_result_t rc;

        ASSERT(db);
        ASSERT(stderr_capture_begin(&cap, &saved) == 0);
        sqlite3_progress_handler(db, 1, interrupt_planned, &plan);
        rc = fl_sqlite_ensure_columns(db, "test", "t", kCols, 2);
        sqlite3_progress_handler(db, 0, NULL, NULL);
        ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
        sqlite3_close(db);
        if (rc == FL_RESULT_OK) {
            finished = 1;
        } else {
            /* The first line names the interrupt (the undo, also interrupted, may add more). */
            ASSERT(rc == FL_RESULT_ERR);
            ASSERT(strncmp(buf, "test: interrupted\n", strlen("test: interrupted\n")) == 0);
            ASSERT(strstr(buf, "does not exist") == NULL);
        }
    }
    ASSERT(finished);
    return 0;
}

/* One failed step anywhere in the migration, with its undo failing too or not, leaves no
 * transaction open on the handle and the table never half-migrated. */
static int check_interrupted_migration(int at, int fail_undo, int *finished) {
    {
        sqlite3 *db = open_mem("CREATE TABLE t (a TEXT); INSERT INTO t VALUES('x');");
        interrupt_plan_t plan = {0, at, 0, fail_undo, 0};
        char buf[512];
        FILE *cap = NULL;
        int saved = -1;
        fl_result_t rc;

        ASSERT(db);
        ASSERT(stderr_capture_begin(&cap, &saved) == 0);
        sqlite3_trace_v2(db, SQLITE_TRACE_STMT, trace_statement, &plan);
        sqlite3_progress_handler(db, 1, interrupt_planned, &plan);
        rc = fl_sqlite_ensure_columns(db, "test", "t", kCols, 2);
        sqlite3_progress_handler(db, 0, NULL, NULL);
        sqlite3_trace_v2(db, 0, NULL, NULL);
        ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
        ASSERT(sqlite3_get_autocommit(db));
        if (rc == FL_RESULT_OK) {
            ASSERT(column_count(db, "t") == 3);
            /* The interrupt came after the last step: the whole migration ran. */
            *finished = plan.n < at;
        } else {
            /* Never half-migrated: unchanged, or (an interrupt after the commit) all of it. */
            ASSERT(rc == FL_RESULT_ERR && buf[0] != '\0');
            ASSERT(column_count(db, "t") == 1 || column_count(db, "t") == 3);
        }
        sqlite3_close(db);
    }
    return 0;
}

static int test_failed_undo_closes_transaction(void) {
    int at;
    int finished = 0;

    for (at = 1; at < 4000 && !finished; at++) {
        int ignored = 0;

        ASSERT(check_interrupted_migration(at, 0, &finished) == 0);
        ASSERT(check_interrupted_migration(at, 1, &ignored) == 0);
    }
    ASSERT(finished);
    return 0;
}

static int test_decl_bounds(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT);");
    char decl[FL_SQLITE_DECL_MAX + 2];
    fl_sqlite_column_t c = {"x", decl};

    ASSERT(db);
    memset(decl, ' ', sizeof decl - 1);
    memcpy(decl, "TEXT", 4);
    decl[FL_SQLITE_DECL_MAX + 1] = '\0';
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", &c, 1) == FL_RESULT_INVAL); /* 129 bytes */
    decl[FL_SQLITE_DECL_MAX] = '\0';
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", &c, 1) == FL_RESULT_OK); /* 128 bytes */
    ASSERT(column_count(db, "t") == 2);
    sqlite3_close(db);
    return 0;
}

static int test_view_is_not_a_table(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT); CREATE VIEW v AS SELECT a FROM t;");
    fl_sqlite_column_t has = {"a", "TEXT"};

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "v", kCols, 2) == FL_RESULT_ERR); /* ALTER on a view fails */
    ASSERT(column_count(db, "v") == 1 && column_count(db, "t") == 1);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "v", &has, 1) == FL_RESULT_OK);
    sqlite3_close(db);
    return 0;
}

static int test_temp_table_does_not_shadow(void) {
    sqlite3 *db = open_mem("CREATE TABLE t (a TEXT); CREATE TEMP TABLE t (a TEXT, note TEXT, stamp INTEGER);");

    ASSERT(db);
    ASSERT(fl_sqlite_ensure_columns(db, "test", "t", kCols, 2) == FL_RESULT_OK);
    ASSERT(scalar(db, "SELECT count(*) FROM pragma_table_info('t', 'main');") == 3);
    ASSERT(scalar(db, "SELECT count(*) FROM pragma_table_info('t', 'temp');") == 3);
    sqlite3_close(db);
    return 0;
}

static int test_exec(void) {
    sqlite3 *db = open_mem(NULL);

    ASSERT(db);
    ASSERT(fl_sqlite_exec(db, "test", "CREATE TABLE t (a);") == FL_RESULT_OK);
    ASSERT(fl_sqlite_exec(db, "test", "CREATE TABLE t (a);") == FL_RESULT_ERR);
    ASSERT(fl_sqlite_exec(db, NULL, "NOT SQL") == FL_RESULT_ERR);
    {
        char buf[256];
        FILE *cap = NULL;
        int saved = -1;
        fl_result_t rc;

        /* Every failure is named on stderr, an unusable argument included. */
        ASSERT(stderr_capture_begin(&cap, &saved) == 0);
        rc = fl_sqlite_exec(NULL, "test", "SELECT 1;");
        ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
        ASSERT(rc == FL_RESULT_ERR && strcmp(buf, "test: no database or statement\n") == 0);
        ASSERT(stderr_capture_begin(&cap, &saved) == 0);
        rc = fl_sqlite_exec(db, "test", NULL);
        ASSERT(stderr_capture_end(cap, saved, buf, sizeof buf) == 0);
        ASSERT(rc == FL_RESULT_ERR && strcmp(buf, "test: no database or statement\n") == 0);
    }
    sqlite3_close(db);
    return 0;
}

int main(void) {
    int rc = 0;

#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_adds_and_idempotent);
    RUN(test_all_present_unchanged);
    RUN(test_mixed_case_existing);
    RUN(test_missing_table_and_null_args);
    RUN(test_bad_identifiers);
    RUN(test_inval_stderr_lines);
    RUN(test_ident_bounds);
    RUN(test_alter_failure_is_error);
    RUN(test_atomic_multi_column);
    RUN(test_repeated_name_refused);
    RUN(test_too_many_columns);
    RUN(test_probe_step_error);
    RUN(test_probe_error_after_rows);
    RUN(test_failed_undo_closes_transaction);
    RUN(test_decl_bounds);
    RUN(test_view_is_not_a_table);
    RUN(test_temp_table_does_not_shadow);
    RUN(test_exec);
    return rc;
}
