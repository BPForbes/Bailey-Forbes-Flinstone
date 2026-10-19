#include "bridge.h"
#include "heritage.h"
#include "mem_domain.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static fl_result_t bridge_open(const char *db_path, sqlite3 **out) {
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS bridges ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  a_key TEXT NOT NULL,"
        "  b_key TEXT NOT NULL,"
        "  state INTEGER NOT NULL,"
        "  accepted_by TEXT NOT NULL DEFAULT '',"
        "  created_at INTEGER NOT NULL DEFAULT 0,"
        "  accepted_at INTEGER NOT NULL DEFAULT 0"
        ");";
    char *err = NULL;

    *out = NULL;
    if (!db_path || !db_path[0])
        return FL_RESULT_INVAL;
    if (sqlite3_open(db_path, out) != SQLITE_OK) {
        sqlite3_close(*out);
        *out = NULL;
        return FL_RESULT_ERR;
    }
    if (sqlite3_exec(*out, schema, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "bridge: %s\n", err);
            sqlite3_free(err);
        }
        sqlite3_close(*out);
        *out = NULL;
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static void bridge_from_row(sqlite3_stmt *st, fl_bridge_t *b) {
    const char *a = (const char *)sqlite3_column_text(st, 1);
    const char *bk = (const char *)sqlite3_column_text(st, 2);
    const char *by = (const char *)sqlite3_column_text(st, 4);

    mem_domain_zero(b, sizeof(*b));
    b->id = (int64_t)sqlite3_column_int64(st, 0);
    if (a)
        strncpy(b->a_key, a, sizeof(b->a_key) - 1);
    if (bk)
        strncpy(b->b_key, bk, sizeof(b->b_key) - 1);
    b->state = (fl_bridge_state_t)sqlite3_column_int(st, 3);
    if (by)
        strncpy(b->accepted_by, by, sizeof(b->accepted_by) - 1);
    b->created_at = (int64_t)sqlite3_column_int64(st, 5);
    b->accepted_at = (int64_t)sqlite3_column_int64(st, 6);
}

#define BRIDGE_COLUMNS "id,a_key,b_key,state,accepted_by,created_at,accepted_at"

static fl_result_t bridge_get_conn(sqlite3 *sql, int64_t id, fl_bridge_t *out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (sqlite3_prepare_v2(sql, "SELECT " BRIDGE_COLUMNS " FROM bridges WHERE id=?;", -1, &st,
                           NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        bridge_from_row(st, out);
        rc = FL_RESULT_OK;
    }
    sqlite3_finalize(st);
    return rc;
}

/* 1 when the unordered pair has a bridge in any of the given states (min_state..). */
static int pair_has_bridge(sqlite3 *sql, const char *a, const char *b, int min_state) {
    sqlite3_stmt *st = NULL;
    int found = 0;

    if (sqlite3_prepare_v2(sql,
                           "SELECT 1 FROM bridges WHERE state>=? AND "
                           "((a_key=? AND b_key=?) OR (a_key=? AND b_key=?)) LIMIT 1;",
                           -1, &st, NULL) != SQLITE_OK)
        return 0;
    sqlite3_bind_int(st, 1, min_state);
    sqlite3_bind_text(st, 2, a, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, b, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, b, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, a, -1, SQLITE_TRANSIENT);
    found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

fl_result_t fl_bridge_request(const char *db_path, const fl_user_db_t *users,
                              const char *actor_key, const char *peer_key, int64_t now,
                              int64_t *id_out) {
    fl_heritage_relation_t rel;
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;

    if (id_out)
        *id_out = 0;
    if (!users || !fl_user_db_find_by_key(users, actor_key) ||
        !fl_user_db_find_by_key(users, peer_key))
        return FL_RESULT_INVAL;
    rel = fl_heritage_relation(actor_key, peer_key);
    if (rel != FL_HERITAGE_REL_SIBLING && rel != FL_HERITAGE_REL_OTHER_BRANCH)
        return FL_RESULT_INVAL;
    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    if (pair_has_bridge(sql, actor_key, peer_key, (int)FL_BRIDGE_STATE_PENDING)) {
        sqlite3_close(sql);
        return FL_RESULT_BUSY;
    }
    if (sqlite3_prepare_v2(sql,
                           "INSERT INTO bridges(a_key,b_key,state,created_at) VALUES(?,?,?,?);",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_text(st, 1, actor_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, peer_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_BRIDGE_STATE_PENDING);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)now);
    rc = sqlite3_step(st) == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
    sqlite3_finalize(st);
    if (rc == FL_RESULT_OK && id_out)
        *id_out = (int64_t)sqlite3_last_insert_rowid(sql);
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_bridge_accept(const char *db_path, const char *actor_key, int64_t id, int64_t now) {
    fl_bridge_t b;
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;

    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = bridge_get_conn(sql, id, &b);
    if (rc != FL_RESULT_OK) {
        sqlite3_close(sql);
        return rc;
    }
    if (b.state != FL_BRIDGE_STATE_PENDING) {
        sqlite3_close(sql);
        return FL_RESULT_BUSY;
    }
    /* The requesting side cannot accept its own request: consent comes from the other branch. */
    if (!fl_heritage_is_ancestor_or_self(actor_key, b.b_key)) {
        sqlite3_close(sql);
        return FL_RESULT_ACCES;
    }
    if (sqlite3_prepare_v2(sql,
                           "UPDATE bridges SET state=?,accepted_by=?,accepted_at=? "
                           "WHERE id=? AND state=?;",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int(st, 1, (int)FL_BRIDGE_STATE_ACTIVE);
    sqlite3_bind_text(st, 2, actor_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)now);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)id);
    sqlite3_bind_int(st, 5, (int)FL_BRIDGE_STATE_PENDING);
    rc = (sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(sql) == 1) ? FL_RESULT_OK
                                                                       : FL_RESULT_ERR;
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_bridge_revoke(const char *db_path, const char *actor_key, int64_t id) {
    fl_bridge_t b;
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;

    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = bridge_get_conn(sql, id, &b);
    if (rc != FL_RESULT_OK) {
        sqlite3_close(sql);
        return rc;
    }
    if (!fl_heritage_is_ancestor_or_self(actor_key, b.a_key) &&
        !fl_heritage_is_ancestor_or_self(actor_key, b.b_key)) {
        sqlite3_close(sql);
        return FL_RESULT_ACCES;
    }
    if (sqlite3_prepare_v2(sql, "DELETE FROM bridges WHERE id=?;", -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)id);
    rc = sqlite3_step(st) == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_bridge_get(const char *db_path, int64_t id, fl_bridge_t *out) {
    sqlite3 *sql = NULL;
    fl_result_t rc;

    if (!out)
        return FL_RESULT_INVAL;
    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    rc = bridge_get_conn(sql, id, out);
    sqlite3_close(sql);
    return rc;
}

int fl_bridge_active_between(const char *db_path, const char *a_key, const char *b_key) {
    sqlite3 *sql = NULL;
    int found;

    if (!fl_heritage_key_valid(a_key) || !fl_heritage_key_valid(b_key))
        return 0;
    if (bridge_open(db_path, &sql) != FL_RESULT_OK)
        return 0;
    found = pair_has_bridge(sql, a_key, b_key, (int)FL_BRIDGE_STATE_ACTIVE);
    sqlite3_close(sql);
    return found;
}

fl_result_t fl_bridge_list_visible(const char *db_path, const char *actor_key,
                                   fl_bridge_visit_fn visit, void *ctx) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;

    if (!visit)
        return FL_RESULT_INVAL;
    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    if (sqlite3_prepare_v2(sql, "SELECT " BRIDGE_COLUMNS " FROM bridges ORDER BY id;", -1, &st,
                           NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        fl_bridge_t b;

        bridge_from_row(st, &b);
        if (fl_heritage_is_ancestor_or_self(actor_key, b.a_key) ||
            fl_heritage_is_ancestor_or_self(actor_key, b.b_key))
            visit(&b, ctx);
    }
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return FL_RESULT_OK;
}

fl_result_t fl_bridge_remove_for(const char *db_path, const char *key_id) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;

    if (!key_id || !key_id[0])
        return FL_RESULT_INVAL;
    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    if (sqlite3_prepare_v2(sql, "DELETE FROM bridges WHERE a_key=? OR b_key=?;", -1, &st, NULL)
        != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_text(st, 1, key_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key_id, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st) == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return rc;
}
