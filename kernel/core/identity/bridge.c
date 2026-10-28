#include "bridge.h"
#include "heritage.h"
#include "keyquorum_identity.h"
#include "fl/hex.h"
#include "fl/mem_asm.h"
#include "mem_domain.h"
#include "password_hash.h"
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
        "  accepted_at INTEGER NOT NULL DEFAULT 0,"
        "  nonce TEXT NOT NULL DEFAULT '',"
        "  request_sig TEXT NOT NULL DEFAULT '',"
        "  accept_sig TEXT NOT NULL DEFAULT ''"
        ");";
    /* Tables written before bridges were signed lack these: add them in place. */
    static const char *const sig_columns[] = {"nonce", "request_sig", "accept_sig"};
    int have[3] = {0, 0, 0};
    sqlite3_stmt *st = NULL;
    size_t i;
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
    if (sqlite3_prepare_v2(*out, "PRAGMA table_info(bridges);", -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char *col = (const char *)sqlite3_column_text(st, 1);

            for (i = 0; col && i < 3u; i++) {
                if (strcmp(col, sig_columns[i]) == 0)
                    have[i] = 1;
            }
        }
        sqlite3_finalize(st);
    }
    for (i = 0; i < 3u; i++) {
        char alter[96];

        if (have[i])
            continue;
        snprintf(alter, sizeof alter, "ALTER TABLE bridges ADD COLUMN %s TEXT NOT NULL DEFAULT '';",
                 sig_columns[i]);
        if (sqlite3_exec(*out, alter, NULL, NULL, NULL) != SQLITE_OK) {
            sqlite3_close(*out);
            *out = NULL;
            return FL_RESULT_ERR;
        }
    }
    return FL_RESULT_OK;
}

/* 1 when secret is the signing key users holds for key_id. */
static int secret_is_for(const fl_user_db_t *users, const char *key_id,
                         const unsigned char *secret) {
    const unsigned char *public_key = fl_user_db_signing_public(users, key_id);
    unsigned char derived[FL_USER_SIGNING_PUBLIC_LEN];
    int ok;

    if (!public_key || !secret || kq_account_public(secret, derived) != KQ_ACCOUNT_OK)
        return 0;
    ok = asm_mem_ct_equal(derived, public_key, sizeof derived) != 0;
    mem_domain_secure_zero(derived, sizeof derived);
    return ok;
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
    {
        const char *nonce = (const char *)sqlite3_column_text(st, 7);
        const char *req = (const char *)sqlite3_column_text(st, 8);
        const char *acc = (const char *)sqlite3_column_text(st, 9);

        if (nonce)
            strncpy(b->nonce_hex, nonce, sizeof(b->nonce_hex) - 1);
        if (req)
            strncpy(b->request_sig, req, sizeof(b->request_sig) - 1);
        if (acc)
            strncpy(b->accept_sig, acc, sizeof(b->accept_sig) - 1);
    }
}

#define BRIDGE_COLUMNS \
    "id,a_key,b_key,state,accepted_by,created_at,accepted_at,nonce,request_sig,accept_sig"

int fl_bridge_verified(const fl_user_db_t *users, const fl_bridge_t *b) {
    unsigned char nonce[FL_BRIDGE_NONCE_LEN];
    unsigned char req[FL_BRIDGE_SIGNATURE_LEN];
    unsigned char acc[FL_BRIDGE_SIGNATURE_LEN];
    const unsigned char *a_public;
    const unsigned char *by_public;

    if (!users || !b || b->state != FL_BRIDGE_STATE_ACTIVE)
        return 0;
    if (!fl_heritage_is_ancestor_or_self(b->accepted_by, b->b_key))
        return 0;
    if (fl_hex_decode(b->nonce_hex, nonce, sizeof nonce) != 0 ||
        fl_hex_decode(b->request_sig, req, sizeof req) != 0 ||
        fl_hex_decode(b->accept_sig, acc, sizeof acc) != 0)
        return 0;
    a_public = fl_user_db_signing_public(users, b->a_key);
    by_public = fl_user_db_signing_public(users, b->accepted_by);
    if (!a_public || !by_public)
        return 0;
    return kq_account_bridge_verify(KQ_ACCOUNT_BRIDGE_REQUEST, b->a_key, b->b_key, b->a_key, nonce,
                                    a_public, req) == 1 &&
           kq_account_bridge_verify(KQ_ACCOUNT_BRIDGE_ACCEPT, b->a_key, b->b_key, b->accepted_by,
                                    nonce, by_public, acc) == 1;
}

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
                              const unsigned char *actor_secret, int64_t *id_out) {
    fl_heritage_relation_t rel;
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;
    char nonce_hex[FL_PASSWORD_SALT_HEX_CHARS + 1];
    unsigned char nonce[FL_BRIDGE_NONCE_LEN];
    unsigned char sig[FL_BRIDGE_SIGNATURE_LEN];
    char sig_hex[2 * FL_BRIDGE_SIGNATURE_LEN + 1];

    if (id_out)
        *id_out = 0;
    if (!users || !fl_user_db_find_by_key(users, actor_key) ||
        !fl_user_db_find_by_key(users, peer_key))
        return FL_RESULT_INVAL;
    rel = fl_heritage_relation(actor_key, peer_key);
    if (rel != FL_HERITAGE_REL_SIBLING && rel != FL_HERITAGE_REL_OTHER_BRANCH)
        return FL_RESULT_INVAL;
    if (!secret_is_for(users, actor_key, actor_secret))
        return FL_RESULT_ACCES;
    /* A fresh random nonce names this bridge, so its signatures stand for it alone. */
    if (fl_password_generate_salt_hex(nonce_hex, sizeof nonce_hex) != 0 ||
        fl_hex_decode(nonce_hex, nonce, sizeof nonce) != 0)
        return FL_RESULT_ERR;
    if (kq_account_bridge_sign(KQ_ACCOUNT_BRIDGE_REQUEST, actor_key, peer_key, actor_key, nonce,
                               actor_secret, sig) != KQ_ACCOUNT_OK)
        return FL_RESULT_ERR;
    fl_hex_encode(sig, sizeof sig, sig_hex);
    rc = bridge_open(db_path, &sql);
    if (rc != FL_RESULT_OK)
        return rc;
    if (pair_has_bridge(sql, actor_key, peer_key, (int)FL_BRIDGE_STATE_PENDING)) {
        sqlite3_close(sql);
        return FL_RESULT_BUSY;
    }
    if (sqlite3_prepare_v2(sql,
                           "INSERT INTO bridges(a_key,b_key,state,created_at,nonce,request_sig) "
                           "VALUES(?,?,?,?,?,?);",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_text(st, 1, actor_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, peer_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_BRIDGE_STATE_PENDING);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)now);
    sqlite3_bind_text(st, 5, nonce_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, sig_hex, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st) == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
    sqlite3_finalize(st);
    if (rc == FL_RESULT_OK && id_out)
        *id_out = (int64_t)sqlite3_last_insert_rowid(sql);
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_bridge_accept(const char *db_path, const fl_user_db_t *users, const char *actor_key,
                             int64_t id, int64_t now, const unsigned char *actor_secret) {
    fl_bridge_t b;
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc;
    unsigned char nonce[FL_BRIDGE_NONCE_LEN];
    unsigned char sig[FL_BRIDGE_SIGNATURE_LEN];
    char sig_hex[2 * FL_BRIDGE_SIGNATURE_LEN + 1];

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
    if (!fl_heritage_is_ancestor_or_self(actor_key, b.b_key) ||
        !secret_is_for(users, actor_key, actor_secret)) {
        sqlite3_close(sql);
        return FL_RESULT_ACCES;
    }
    /* A request with no valid signature from its requester cannot be accepted. */
    {
        const unsigned char *a_public = fl_user_db_signing_public(users, b.a_key);
        unsigned char req[FL_BRIDGE_SIGNATURE_LEN];

        if (fl_hex_decode(b.nonce_hex, nonce, sizeof nonce) != 0 ||
            fl_hex_decode(b.request_sig, req, sizeof req) != 0 || !a_public ||
            kq_account_bridge_verify(KQ_ACCOUNT_BRIDGE_REQUEST, b.a_key, b.b_key, b.a_key, nonce,
                                     a_public, req) != 1) {
            sqlite3_close(sql);
            return FL_RESULT_INVAL;
        }
    }
    if (kq_account_bridge_sign(KQ_ACCOUNT_BRIDGE_ACCEPT, b.a_key, b.b_key, actor_key, nonce,
                               actor_secret, sig) != KQ_ACCOUNT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    fl_hex_encode(sig, sizeof sig, sig_hex);
    if (sqlite3_prepare_v2(sql,
                           "UPDATE bridges SET state=?,accepted_by=?,accepted_at=?,accept_sig=? "
                           "WHERE id=? AND state=?;",
                           -1, &st, NULL) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int(st, 1, (int)FL_BRIDGE_STATE_ACTIVE);
    sqlite3_bind_text(st, 2, actor_key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)now);
    sqlite3_bind_text(st, 4, sig_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 5, (sqlite3_int64)id);
    sqlite3_bind_int(st, 6, (int)FL_BRIDGE_STATE_PENDING);
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

int fl_bridge_active_between(const fl_user_db_t *users, const char *a_key, const char *b_key) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int found = 0;

    if (!users || !fl_heritage_key_valid(a_key) || !fl_heritage_key_valid(b_key))
        return 0;
    if (bridge_open(users->path, &sql) != FL_RESULT_OK)
        return 0;
    if (sqlite3_prepare_v2(sql,
                           "SELECT " BRIDGE_COLUMNS " FROM bridges WHERE state=? AND "
                           "((a_key=? AND b_key=?) OR (a_key=? AND b_key=?));",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, (int)FL_BRIDGE_STATE_ACTIVE);
        sqlite3_bind_text(st, 2, a_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, b_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, b_key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 5, a_key, -1, SQLITE_TRANSIENT);
        while (!found && sqlite3_step(st) == SQLITE_ROW) {
            fl_bridge_t b;

            bridge_from_row(st, &b);
            found = fl_bridge_verified(users, &b);
        }
        sqlite3_finalize(st);
    }
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
