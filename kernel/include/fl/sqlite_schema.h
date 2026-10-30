#ifndef FL_SQLITE_SCHEMA_H
#define FL_SQLITE_SCHEMA_H

/**
 * The one SQLite exec and column-ensure helper. Header-only so every target
 * that already links sqlite3 shares it without another object.
 *
 * fl_sqlite_exec runs a fixed statement list and reports the SQLite message on
 * stderr as "tag: message". fl_sqlite_ensure_columns adds the columns a table
 * lacks (stores written before a column existed). Identifiers cannot be bound
 * parameters, so table and column names are checked against
 * [A-Za-z_][A-Za-z0-9_]* (at most FL_SQLITE_IDENT_MAX bytes) and the type text
 * against a closed character set before any SQL is built; the probe binds the
 * table name. The column-ensure runs its ALTERs in one SAVEPOINT (all or
 * nothing) and never closes the handle. If undoing them fails too, a transaction the
 * helper opened is rolled back whole; inside a caller's transaction the caller must
 * roll back or close the handle on FL_RESULT_ERR. A failure reported after the final
 * RELEASE committed leaves every column added: the table is never half-migrated. The fl_sqlite_priv_* validators are
 * internal; use only fl_sqlite_exec and fl_sqlite_ensure_columns.
 */

#include "contract_result.h"
#include <sqlite3.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_SQLITE_IDENT_MAX 64u
#define FL_SQLITE_DECL_MAX 128u

/** One column to add when missing; decl is e.g. "TEXT NOT NULL DEFAULT ''". */
typedef struct {
    const char *name;
    const char *decl;
} fl_sqlite_column_t;

/** Run stmt; on any failure print "tag: message" to stderr (tag may be NULL). */
static inline fl_result_t fl_sqlite_exec(sqlite3 *db, const char *tag, const char *stmt) {
    char *err = NULL;
    int rc;

    if (!db || !stmt) {
        fprintf(stderr, "%s: no database or statement\n", tag ? tag : "sqlite");
        return FL_RESULT_ERR;
    }
    rc = sqlite3_exec(db, stmt, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        /* SQLite sets no message on some failures (out of memory): name the code instead. */
        fprintf(stderr, "%s: %s\n", tag ? tag : "sqlite", err ? err : sqlite3_errstr(rc));
        sqlite3_free(err);
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

/** Internal (not API): 1 when s is [A-Za-z_][A-Za-z0-9_]* and at most FL_SQLITE_IDENT_MAX bytes. */
static inline int fl_sqlite_priv_ident_ok(const char *s) {
    size_t i;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        int alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        int digit = c >= '0' && c <= '9';

        if (i >= FL_SQLITE_IDENT_MAX || !(alpha || (i > 0 && digit)))
            return 0;
    }
    return 1;
}

/**
 * Internal (not API): 1 when s is non-empty, at most FL_SQLITE_DECL_MAX bytes, built from letters, digits,
 * space, _ and ' only, with an even number of ' (string literals closed, '' for an embedded quote). So no
 * statement separator, comment, quoted identifier or parenthesised expression (no DEFAULT (func(...)),
 * CHECK (...) or REFERENCES t(col)); bare keywords such as REFERENCES t or COLLATE NOCASE are not
 * refused. Callers pass constants, e.g. "TEXT NOT NULL DEFAULT ''".
 */
static inline int fl_sqlite_priv_decl_ok(const char *s) {
    size_t i;
    unsigned quotes = 0;

    if (!s || !s[0])
        return 0;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == ' ' || c == '_' || c == '\'';

        if (i >= FL_SQLITE_DECL_MAX || !ok)
            return 0;
        if (c == '\'')
            quotes++;
    }
    return (quotes & 1u) == 0;
}

/* Internal (not API): undo the ensure's savepoint. If that fails and the helper opened the transaction
 * (own), roll the whole transaction back so the handle is never left inside a half-migrated one. */
static inline void fl_sqlite_priv_abort(sqlite3 *db, const char *tag, int own) {
    if (fl_sqlite_exec(db, tag, "ROLLBACK TO fl_ensure_columns; RELEASE fl_ensure_columns;") != FL_RESULT_OK &&
        own && !sqlite3_get_autocommit(db))
        (void)fl_sqlite_exec(db, tag, "ROLLBACK;");
}

static inline fl_result_t fl_sqlite_priv_inval(const char *tag, const char *what) {
    fprintf(stderr, "%s: invalid column definition (%s)\n", tag ? tag : "sqlite", what);
    return FL_RESULT_INVAL;
}

/**
 * Add each column of cols[0..n) that table (in schema "main", so a TEMP table of the same name never
 * shadows it) lacks; names compare ASCII case-insensitively, as SQLite does. Every identifier and type is
 * checked first, and a repeated name is refused (FL_RESULT_INVAL, a stderr line, nothing written). The
 * ALTERs run inside one SAVEPOINT (nests inside a caller's transaction): if one fails all are rolled back,
 * so no table is left half-migrated (see the header comment for a failed undo). A table that does not
 * exist, a failed probe or a failed ALTER is FL_RESULT_ERR; create the table first.
 */
static inline fl_result_t fl_sqlite_ensure_columns(sqlite3 *db, const char *tag, const char *table,
                                                   const fl_sqlite_column_t *cols, size_t n) {
    enum { MAX_COLS = 32 };
    char have[MAX_COLS] = {0};
    sqlite3_stmt *st = NULL;
    int rows = 0;
    int missing = 0;
    int own;
    int rc;
    size_t i, j;

    if (!db || !cols || n == 0)
        return FL_RESULT_INVAL;
    if (!fl_sqlite_priv_ident_ok(table))
        return fl_sqlite_priv_inval(tag, "table name");
    if (n > MAX_COLS)
        return fl_sqlite_priv_inval(tag, "too many columns");
    for (i = 0; i < n; i++) {
        if (!fl_sqlite_priv_ident_ok(cols[i].name))
            return fl_sqlite_priv_inval(tag, "column name");
        if (!fl_sqlite_priv_decl_ok(cols[i].decl))
            return fl_sqlite_priv_inval(tag, "column type");
        for (j = 0; j < i; j++) {
            if (sqlite3_stricmp(cols[j].name, cols[i].name) == 0)
                return fl_sqlite_priv_inval(tag, "repeated column name");
        }
    }
    if (sqlite3_prepare_v2(db, "SELECT name FROM pragma_table_info(?1, 'main');", -1, &st, NULL) != SQLITE_OK ||
        sqlite3_bind_text(st, 1, table, -1, SQLITE_STATIC) != SQLITE_OK) {
        fprintf(stderr, "%s: %s\n", tag ? tag : "sqlite", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        const char *col = (const char *)sqlite3_column_text(st, 0);

        rows++;
        for (i = 0; col && i < n; i++) {
            if (sqlite3_stricmp(col, cols[i].name) == 0)
                have[i] = 1;
        }
    }
    if (rc != SQLITE_DONE)
        fprintf(stderr, "%s: %s\n", tag ? tag : "sqlite", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE)
        return FL_RESULT_ERR;
    if (rows == 0) {
        fprintf(stderr, "%s: table %s does not exist\n", tag ? tag : "sqlite", table);
        return FL_RESULT_ERR;
    }
    for (i = 0; i < n; i++)
        missing += !have[i];
    if (missing == 0)
        return FL_RESULT_OK;
    own = sqlite3_get_autocommit(db);
    if (fl_sqlite_exec(db, tag, "SAVEPOINT fl_ensure_columns;") != FL_RESULT_OK) {
        /* A failed SAVEPOINT can still have opened the transaction. */
        if (own && !sqlite3_get_autocommit(db))
            (void)fl_sqlite_exec(db, tag, "ROLLBACK;");
        return FL_RESULT_ERR;
    }
    for (i = 0; i < n; i++) {
        char alter[512];
        size_t want;

        if (have[i])
            continue;
        want = strlen("ALTER TABLE main.\"\" ADD COLUMN \"\" ;") + strlen(table) + strlen(cols[i].name) +
               strlen(cols[i].decl);
        if (want >= sizeof alter) {
            fl_sqlite_priv_abort(db, tag, own);
            return fl_sqlite_priv_inval(tag, "statement too long");
        }
        sqlite3_snprintf((int)sizeof alter, alter, "ALTER TABLE main.\"%w\" ADD COLUMN \"%w\" %s;", table,
                         cols[i].name, cols[i].decl);
        if (strlen(alter) != want) {
            fl_sqlite_priv_abort(db, tag, own);
            return fl_sqlite_priv_inval(tag, "statement length");
        }
        if (fl_sqlite_exec(db, tag, alter) != FL_RESULT_OK) {
            fl_sqlite_priv_abort(db, tag, own);
            return FL_RESULT_ERR;
        }
    }
    if (fl_sqlite_exec(db, tag, "RELEASE fl_ensure_columns;") != FL_RESULT_OK) {
        fl_sqlite_priv_abort(db, tag, own);
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

#ifdef __cplusplus
}
#endif

#endif
