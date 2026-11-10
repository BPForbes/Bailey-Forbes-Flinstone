#include "user_db.h"
#include "heritage.h"
#include "fl/driver/keyquorum.h"
#include "password_hash.h"
#include "mem_domain.h"
#include "fl/hex.h"
#include "fl/mem_asm.h"
#include "fl/sqlite_schema.h"
#include <limits.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const k_user_db_schema =
    "CREATE TABLE IF NOT EXISTS users ("
    "  name TEXT PRIMARY KEY NOT NULL,"
    "  password_hash TEXT NOT NULL,"
    "  salt_hex TEXT NOT NULL,"
    "  uid INTEGER NOT NULL,"
    "  is_elevated INTEGER NOT NULL DEFAULT 0,"
    "  key_id TEXT NOT NULL DEFAULT '',"
    "  created_by TEXT NOT NULL DEFAULT '',"
    "  created_at INTEGER NOT NULL DEFAULT 0,"
    "  signing_public TEXT NOT NULL DEFAULT '',"
    "  signing_sealed TEXT NOT NULL DEFAULT '',"
    "  box_public TEXT NOT NULL DEFAULT '',"
    "  box_sealed TEXT NOT NULL DEFAULT '',"
    "  auth_mode INTEGER NOT NULL DEFAULT 0"
    ");"
    /* Key metadata, written when a key is minted, paired with its KQST. The one home of the KQST path. */
    "CREATE TABLE IF NOT EXISTS account_keys ("
    "  label TEXT PRIMARY KEY NOT NULL, purpose TEXT NOT NULL, holds_share INTEGER NOT NULL,"
    "  minted_by TEXT NOT NULL, minted_at INTEGER NOT NULL, generation INTEGER NOT NULL,"
    "  kqst_path TEXT, device_id BLOB NOT NULL, signing_public BLOB NOT NULL,"
    "  encryption_public BLOB NOT NULL, ledger_seq INTEGER NOT NULL, event_hash BLOB NOT NULL"
    ");"
    /* Live private bridges, mirroring KeyQuorum's (not the account-to-account `bridges` table of
     * bridge.c). A sealed bridge key is kept only for members whose account exists on this machine. */
    "CREATE TABLE IF NOT EXISTS account_bridges ("
    "  uid TEXT PRIMARY KEY NOT NULL, generation INTEGER NOT NULL, salt BLOB NOT NULL,"
    "  public_key BLOB NOT NULL, destroyed INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS account_bridge_members ("
    "  uid TEXT NOT NULL, label TEXT NOT NULL, encryption_public BLOB NOT NULL,"
    "  signing_public BLOB NOT NULL, sealed_bridge_key BLOB, wrap_salt BLOB, PRIMARY KEY (uid, label)"
    ");"
    /* A parent account's key assembled from its children's shares: sealed, safe at rest. */
    "CREATE TABLE IF NOT EXISTS account_assembly ("
    "  parent_label TEXT PRIMARY KEY NOT NULL, threshold INTEGER NOT NULL, generation INTEGER NOT NULL,"
    "  sealed_shares BLOB NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS meta ("
    "  key TEXT PRIMARY KEY NOT NULL,"
    "  value TEXT NOT NULL"
    ");";

/* Stores written before heritage key ids or account keys lack these columns: add them in place. */
static const fl_sqlite_column_t heritage_columns[] = {
    {"key_id", "TEXT NOT NULL DEFAULT ''"},
    {"created_by", "TEXT NOT NULL DEFAULT ''"},
    {"created_at", "INTEGER NOT NULL DEFAULT 0"},
    {"signing_public", "TEXT NOT NULL DEFAULT ''"},
    {"signing_sealed", "TEXT NOT NULL DEFAULT ''"},
    {"box_public", "TEXT NOT NULL DEFAULT ''"},
    {"box_sealed", "TEXT NOT NULL DEFAULT ''"},
    {"auth_mode", "INTEGER NOT NULL DEFAULT 0"}, /* an old store upgrades in place: every row reads 0 */
};

/* A bridge member's sealed key is opened with the wrap salt stored beside it (keyquorum_bridge.h);
 * stores written before the salt was kept gain the column, and their sealed keys (none were ever
 * written) stay unusable until stored again with it. */
static const fl_sqlite_column_t bridge_member_columns[] = {
    {"wrap_salt", "BLOB"},
};

static fl_result_t db_init_schema(sqlite3 *sql) {
    if (fl_sqlite_exec(sql, "user_db", k_user_db_schema) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_sqlite_ensure_columns(sql, "user_db", "users", heritage_columns,
                                 sizeof(heritage_columns) / sizeof(heritage_columns[0])) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return fl_sqlite_ensure_columns(sql, "user_db", "account_bridge_members", bridge_member_columns,
                                    sizeof(bridge_member_columns) / sizeof(bridge_member_columns[0]));
}

static fl_result_t db_read_meta_default(sqlite3 *db, char *out, size_t outsz) {
    sqlite3_stmt *st = NULL;
    const char *query = "SELECT value FROM meta WHERE key='default_user' LIMIT 1;";
    if (sqlite3_prepare_v2(db, query, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    out[0] = '\0';
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);
        if (v)
            strncpy(out, v, outsz - 1);
    }
    sqlite3_finalize(st);
    out[outsz - 1] = '\0';
    return FL_RESULT_OK;
}

static fl_result_t db_write_meta_default(sqlite3 *sql, const char *user) {
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO meta(key,value) VALUES('default_user',?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
    if (!user)
        return FL_RESULT_INVAL;
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, user, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

static fl_result_t hash_password_fields(const char *password, char *hash_hex, size_t hash_sz,
                                        char *salt_hex, size_t salt_sz) {
    if (fl_password_generate_salt_hex(salt_hex, salt_sz) != 0)
        return FL_RESULT_ERR;
    if (fl_password_hash_password(password, salt_hex, hash_hex, hash_sz) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

/* A new box key for u (what file letters are sealed to), sealed under password. */
static fl_result_t provision_box(fl_user_record_t *u, const char *password) {
    unsigned char secret[FL_USER_SIGNING_SECRET_LEN];
    unsigned char sealed[FL_USER_SIGNING_SEALED_LEN];
    size_t sealed_len = 0;
    fl_result_t rc = FL_RESULT_ERR;
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();

    if (kq && kq->account_box_keygen(secret, u->box_public) == FL_KQ_ACCOUNT_OK &&
        kq->account_seal(password, secret, sealed, sizeof sealed, &sealed_len) ==
            FL_KQ_ACCOUNT_OK &&
        sealed_len == sizeof sealed) {
        memcpy(u->box_sealed, sealed, sizeof sealed);
        u->has_box = 1;
        rc = FL_RESULT_OK;
    }
    mem_domain_secure_zero(secret, sizeof secret);
    if (rc != FL_RESULT_OK) {
        u->has_box = 0;
        mem_domain_zero(u->box_public, sizeof u->box_public);
        mem_domain_zero(u->box_sealed, sizeof u->box_sealed);
    }
    return rc;
}

/* A new signing key and box key for u, both sealed under password. */
static fl_result_t provision_key(fl_user_record_t *u, const char *password) {
    unsigned char secret[FL_USER_SIGNING_SECRET_LEN];
    unsigned char sealed[FL_USER_SIGNING_SEALED_LEN];
    size_t sealed_len = 0;
    fl_result_t rc = FL_RESULT_ERR;
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();

    if (kq && kq->account_keygen(secret, u->signing_public) == FL_KQ_ACCOUNT_OK &&
        kq->account_seal(password, secret, sealed, sizeof sealed, &sealed_len) ==
            FL_KQ_ACCOUNT_OK &&
        sealed_len == sizeof sealed) {
        memcpy(u->signing_sealed, sealed, sizeof sealed);
        u->has_key = 1;
        rc = FL_RESULT_OK;
    }
    mem_domain_zero(secret, sizeof secret);
    if (rc == FL_RESULT_OK)
        rc = provision_box(u, password);
    if (rc != FL_RESULT_OK) {
        u->has_key = 0;
        mem_domain_zero(u->signing_public, sizeof u->signing_public);
        mem_domain_zero(u->signing_sealed, sizeof u->signing_sealed);
    }
    return rc;
}

static fl_result_t db_upsert_user(sqlite3 *sql, const fl_user_record_t *u) {
    sqlite3_stmt *st = NULL;
    char public_hex[2 * FL_USER_SIGNING_PUBLIC_LEN + 1] = "";
    char sealed_hex[2 * FL_USER_SIGNING_SEALED_LEN + 1] = "";
    char box_public_hex[2 * FL_USER_SIGNING_PUBLIC_LEN + 1] = "";
    char box_sealed_hex[2 * FL_USER_SIGNING_SEALED_LEN + 1] = "";
    const char *ins =
        "INSERT INTO users(name,password_hash,salt_hex,uid,is_elevated,key_id,created_by,created_at,"
        "signing_public,signing_sealed,box_public,box_sealed,auth_mode) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET password_hash=excluded.password_hash,"
        "salt_hex=excluded.salt_hex,uid=excluded.uid,is_elevated=excluded.is_elevated,"
        "key_id=excluded.key_id,created_by=excluded.created_by,created_at=excluded.created_at,"
        "signing_public=excluded.signing_public,signing_sealed=excluded.signing_sealed,"
        "box_public=excluded.box_public,box_sealed=excluded.box_sealed,auth_mode=excluded.auth_mode;";
    if (u->has_key) {
        fl_hex_encode(u->signing_public, sizeof u->signing_public, public_hex);
        fl_hex_encode(u->signing_sealed, sizeof u->signing_sealed, sealed_hex);
    }
    if (u->has_box) {
        fl_hex_encode(u->box_public, sizeof u->box_public, box_public_hex);
        fl_hex_encode(u->box_sealed, sizeof u->box_sealed, box_sealed_hex);
    }
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, u->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, u->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, u->salt_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, (int)u->uid);
    sqlite3_bind_int(st, 5, u->is_elevated ? 1 : 0);
    sqlite3_bind_text(st, 6, u->key_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, u->created_by, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)u->created_at);
    sqlite3_bind_text(st, 9, public_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, sealed_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, box_public_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 12, box_sealed_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 13, u->auth_mode);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

static fl_result_t db_load_users(sqlite3 *sql, fl_user_db_t *db) {
    sqlite3_stmt *st = NULL;
    const char *q = "SELECT name,password_hash,salt_hex,uid,is_elevated,key_id,created_by,created_at,"
                    "signing_public,signing_sealed,box_public,box_sealed,auth_mode FROM users ORDER BY uid;";
    if (sqlite3_prepare_v2(sql, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    db->count = 0;
    for (;;) {
        int step_rc = sqlite3_step(st);

        if (step_rc == SQLITE_DONE)
            break;
        if (step_rc != SQLITE_ROW) {
            sqlite3_finalize(st);
            return FL_RESULT_ERR;
        }
        if (db->count >= FL_USER_DB_MAX_USERS) {
            sqlite3_finalize(st);
            return FL_RESULT_ERR;
        }
        {
            fl_user_record_t *u = &db->users[db->count];
            const char *name = (const char *)sqlite3_column_text(st, 0);
            const char *hash = (const char *)sqlite3_column_text(st, 1);
            const char *salt = (const char *)sqlite3_column_text(st, 2);

            mem_domain_zero(u, sizeof(*u));
            if (name)
                strncpy(u->name, name, sizeof(u->name) - 1);
            if (hash)
                strncpy(u->password_hash, hash, sizeof(u->password_hash) - 1);
            if (salt)
                strncpy(u->salt_hex, salt, sizeof(u->salt_hex) - 1);
            u->uid = (uint32_t)sqlite3_column_int(st, 3);
            u->is_elevated = sqlite3_column_int(st, 4) ? 1 : 0;
            {
                const char *key = (const char *)sqlite3_column_text(st, 5);
                const char *by = (const char *)sqlite3_column_text(st, 6);

                if (key)
                    strncpy(u->key_id, key, sizeof(u->key_id) - 1);
                if (by)
                    strncpy(u->created_by, by, sizeof(u->created_by) - 1);
            }
            u->created_at = (int64_t)sqlite3_column_int64(st, 7);
            /* A key counts only when both halves are well formed; anything else is no key. */
            u->has_key = fl_hex_decode((const char *)sqlite3_column_text(st, 8), u->signing_public,
                                  sizeof u->signing_public) == 0 &&
                         fl_hex_decode((const char *)sqlite3_column_text(st, 9), u->signing_sealed,
                                  sizeof u->signing_sealed) == 0;
            if (!u->has_key) {
                mem_domain_zero(u->signing_public, sizeof u->signing_public);
                mem_domain_zero(u->signing_sealed, sizeof u->signing_sealed);
            }
            u->has_box = fl_hex_decode((const char *)sqlite3_column_text(st, 10), u->box_public,
                                  sizeof u->box_public) == 0 &&
                         fl_hex_decode((const char *)sqlite3_column_text(st, 11), u->box_sealed,
                                  sizeof u->box_sealed) == 0;
            if (!u->has_box) {
                mem_domain_zero(u->box_public, sizeof u->box_public);
                mem_domain_zero(u->box_sealed, sizeof u->box_sealed);
            }
            u->auth_mode = sqlite3_column_int(st, 12) == FL_USER_AUTH_PASSWORD_KQST ? FL_USER_AUTH_PASSWORD_KQST
                                                                                    : FL_USER_AUTH_PASSWORD;
            db->count++;
        }
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

static int user_db_lab_defaults_enabled(void) {
    const char *v = getenv("FL_USERS_LAB_DEFAULTS");
    return v && v[0] && v[0] != '0';
}

#define FL_USER_DB_LAB_DEFAULT_USER "Flinstone"

/*
 * Lab accounts: Root (the master, key Root), Flinstone (Root.Flinstone) and
 * Alternative (Root.Alternative). Passwords are the published lab values, so
 * they are only ever written when FL_USERS_LAB_DEFAULTS is set.
 */
static fl_result_t seed_lab_accounts(fl_user_db_t *db) {
    fl_result_t rc;

    if (!fl_user_db_find(db, FL_HERITAGE_ROOT_KEY)) {
        rc = fl_user_db_add_user(db, FL_HERITAGE_ROOT_KEY, "root", 0, 1);
        if (rc != FL_RESULT_OK)
            return rc;
    }
    if (!fl_user_db_find(db, "Flinstone")) {
        rc = fl_user_db_add_user_under(db, FL_HERITAGE_ROOT_KEY, "Flinstone", "flinstone", 1000,
                                       0, 0);
        if (rc != FL_RESULT_OK)
            return rc;
    }
    if (!fl_user_db_find(db, "Alternative")) {
        uint32_t uid = 1001;
        size_t i;

        for (i = 0; i < db->count; i++) {
            if (db->users[i].uid == uid) {
                uid = fl_user_db_next_uid(db);
                break;
            }
        }
        rc = fl_user_db_add_user_under(db, FL_HERITAGE_ROOT_KEY, "Alternative", "alternative", uid,
                                       0, 0);
        if (rc != FL_RESULT_OK)
            return rc;
    }
    return FL_RESULT_OK;
}

void fl_user_db_seed_defaults(fl_user_db_t *db) {
    if (!db)
        return;
    fl_user_db_clear(db);
    strncpy(db->default_user, FL_USER_DB_LAB_DEFAULT_USER, sizeof(db->default_user) - 1);
    if (!user_db_lab_defaults_enabled()) {
        fprintf(stderr,
                "user_db: refusing known default passwords; set FL_USERS_LAB_DEFAULTS=1 for lab seeds\n");
        db->loaded = 0;
        return;
    }
    if (seed_lab_accounts(db) != FL_RESULT_OK) {
        fl_user_db_clear(db);
        strncpy(db->default_user, FL_USER_DB_LAB_DEFAULT_USER, sizeof(db->default_user) - 1);
        return;
    }
    db->loaded = 1;
}

/*
 * Two stored names that differ only by case (the old lookup and primary key were
 * case-sensitive, so root and Root could both exist) would both become Root.
 * The rewrite would then overwrite one record with the other, so the store is
 * refused instead, and left unchanged.
 */
static int find_case_collision(const fl_user_db_t *db, size_t *first, size_t *second) {
    size_t i;
    size_t j;

    for (i = 0; i < db->count; i++) {
        for (j = i + 1; j < db->count; j++) {
            if (strcasecmp(db->users[i].name, db->users[j].name) == 0) {
                *first = i;
                *second = j;
                return 1;
            }
        }
    }
    return 0;
}

/*
 * Accounts stored before heritage key ids have none. root becomes the master
 * Root, flinstone becomes Flinstone (Root.Flinstone), and every other account
 * is placed directly under Root: who created it was never recorded, and a
 * child of Root reaches no other account's home without a bridge.
 * Returns 1 when anything changed.
 */
static int migrate_heritage(fl_user_db_t *db) {
    size_t i;
    int changed = 0;

    for (i = 0; i < db->count; i++) {
        fl_user_record_t *u = &db->users[i];

        if (u->key_id[0])
            continue;
        if (strcasecmp(u->name, FL_HERITAGE_ROOT_KEY) == 0) {
            strncpy(u->name, FL_HERITAGE_ROOT_KEY, sizeof(u->name) - 1);
            strncpy(u->key_id, FL_HERITAGE_ROOT_KEY, sizeof(u->key_id) - 1);
            u->created_by[0] = '\0';
        } else {
            if (strcasecmp(u->name, "flinstone") == 0)
                strncpy(u->name, "Flinstone", sizeof(u->name) - 1);
            if (fl_heritage_child(FL_HERITAGE_ROOT_KEY, u->name, u->key_id, sizeof(u->key_id))
                != FL_RESULT_OK) {
                /* A legacy name that is not a valid segment keeps no key id: it can log in
                 * but holds no authority over, and no bridge to, any other account. */
                fprintf(stderr, "user_db: account '%s' has no heritage key id (name not [A-Za-z0-9_-])\n",
                        u->name);
                continue;
            }
            strncpy(u->created_by, FL_HERITAGE_ROOT_KEY, sizeof(u->created_by) - 1);
        }
        changed = 1;
    }
    if (db->default_user[0]) {
        const fl_user_record_t *def = fl_user_db_find(db, db->default_user);

        if (def && strcmp(def->name, db->default_user) != 0) {
            strncpy(db->default_user, def->name, sizeof(db->default_user) - 1);
            changed = 1;
        }
    }
    return changed;
}

static fl_result_t db_save_conn(sqlite3 *sql, const fl_user_db_t *db) {
    size_t i;
    fl_result_t rc = FL_RESULT_OK;

    if (fl_sqlite_exec(sql, "user_db", "BEGIN TRANSACTION;") != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_sqlite_exec(sql, "user_db", "DELETE FROM users;") != FL_RESULT_OK) {
        (void)fl_sqlite_exec(sql, "user_db", "ROLLBACK;");
        return FL_RESULT_ERR;
    }
    for (i = 0; i < db->count; i++) {
        if (db_upsert_user(sql, &db->users[i]) != FL_RESULT_OK) {
            rc = FL_RESULT_ERR;
            break;
        }
    }
    if (rc == FL_RESULT_OK && db->default_user[0])
        rc = db_write_meta_default(sql, db->default_user);
    if (rc == FL_RESULT_OK) {
        if (fl_sqlite_exec(sql, "user_db", "COMMIT;") != FL_RESULT_OK)
            rc = FL_RESULT_ERR;
    } else {
        (void)fl_sqlite_exec(sql, "user_db", "ROLLBACK;");
    }
    return rc;
}

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path) {
    sqlite3 *sql = NULL;
    fl_result_t rc;
    int changed;

    if (!db || !path)
        return FL_RESULT_INVAL;
    fl_user_db_clear(db);
    strncpy(db->path, path, sizeof(db->path) - 1);

    if (sqlite3_open(path, &sql) != SQLITE_OK) {
        /* sqlite3_open allocates a handle even when it fails; it must still be closed. */
        sqlite3_close(sql);
        return FL_RESULT_NOENT;
    }
    if (db_init_schema(sql) != FL_RESULT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }

    rc = db_load_users(sql, db);
    if (rc != FL_RESULT_OK) {
        sqlite3_close(sql);
        return rc;
    }

    if (db->count == 0) {
        if (!user_db_lab_defaults_enabled()) {
            sqlite3_close(sql);
            return FL_RESULT_ERR;
        }
        strncpy(db->default_user, FL_USER_DB_LAB_DEFAULT_USER, sizeof(db->default_user) - 1);
        if (seed_lab_accounts(db) != FL_RESULT_OK || db_save_conn(sql, db) != FL_RESULT_OK) {
            sqlite3_close(sql);
            return FL_RESULT_ERR;
        }
        fl_user_db_clear(db);
        strncpy(db->path, path, sizeof(db->path) - 1);
        rc = db_load_users(sql, db);
    }

    (void)db_read_meta_default(sql, db->default_user, sizeof(db->default_user));
    if (!db->default_user[0] && db->count > 0)
        strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);

    if (rc == FL_RESULT_OK) {
        size_t a = 0;
        size_t b = 0;

        if (find_case_collision(db, &a, &b)) {
            fprintf(stderr,
                    "user_db: accounts '%s' and '%s' differ only by case; rename one and start again "
                    "(the store was not changed)\n",
                    db->users[a].name, db->users[b].name);
            rc = FL_RESULT_ERR;
        }
    }
    if (rc == FL_RESULT_OK) {
        changed = migrate_heritage(db);
        if (user_db_lab_defaults_enabled() && !fl_user_db_find(db, "Alternative") &&
            fl_user_db_find(db, FL_HERITAGE_ROOT_KEY)) {
            if (seed_lab_accounts(db) == FL_RESULT_OK)
                changed = 1;
        }
        if (changed && db_save_conn(sql, db) != FL_RESULT_OK)
            rc = FL_RESULT_ERR;
    }

    if (rc == FL_RESULT_OK)
        db->loaded = 1;
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_user_db_save(const fl_user_db_t *db) {
    sqlite3 *sql = NULL;
    const char *path;
    fl_result_t rc;

    if (!db || !db->loaded)
        return FL_RESULT_INVAL;
    path = db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH;
    if (sqlite3_open(path, &sql) != SQLITE_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    if (db_init_schema(sql) != FL_RESULT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    rc = db_save_conn(sql, db);
    sqlite3_close(sql);
    return rc;
}

void fl_user_db_clear(fl_user_db_t *db) {
    if (!db)
        return;
    mem_domain_zero(db, sizeof(*db));
}

const fl_user_record_t *fl_user_db_find(const fl_user_db_t *db, const char *name) {
    size_t i;
    if (!db || !name || !name[0])
        return NULL;
    for (i = 0; i < db->count; i++) {
        if (strcasecmp(db->users[i].name, name) == 0)
            return &db->users[i];
    }
    return NULL;
}

const fl_user_record_t *fl_user_db_find_by_key(const fl_user_db_t *db, const char *key_id) {
    size_t i;
    if (!db || !key_id || !key_id[0])
        return NULL;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].key_id, key_id) == 0)
            return &db->users[i];
    }
    return NULL;
}

int fl_user_db_has_descendants(const fl_user_db_t *db, const char *key_id) {
    size_t i;
    if (!db || !fl_heritage_key_valid(key_id))
        return 0;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].key_id, key_id) != 0 &&
            fl_heritage_is_ancestor_or_self(key_id, db->users[i].key_id))
            return 1;
    }
    return 0;
}

int fl_user_db_verify_password(const fl_user_db_t *db, const char *name, const char *password) {
    const fl_user_record_t *u;
    if (!db || !name || !password)
        return 0;
    u = fl_user_db_find(db, name);
    if (!u)
        return 0;
    return fl_password_verify(password, u->salt_hex, u->password_hash);
}

int fl_user_db_is_elevated_user(const fl_user_db_t *db, const char *name) {
    const fl_user_record_t *u = fl_user_db_find(db, name);
    return (u && u->is_elevated) ? 1 : 0;
}

uint32_t fl_user_db_next_uid(const fl_user_db_t *db) {
    uint32_t candidate = FL_USER_DB_MIN_ALLOC_UID;
    size_t i;

    if (!db)
        return FL_USER_DB_MIN_ALLOC_UID;
    for (;;) {
        int taken = 0;
        for (i = 0; i < db->count; i++) {
            if (db->users[i].uid == candidate) {
                taken = 1;
                break;
            }
        }
        if (!taken)
            return candidate;
        if (candidate == UINT32_MAX)
            break;
        candidate++;
    }
    return FL_USER_DB_MIN_ALLOC_UID;
}

static fl_result_t add_user_record(fl_user_db_t *db, const char *name, const char *password,
                                   uint32_t uid, int is_elevated, const char *key_id,
                                   const char *created_by, int64_t created_at) {
    fl_user_record_t *u;
    size_t i;
    if (!db || !name || !name[0] || !password || !key_id)
        return FL_RESULT_INVAL;
    if (strlen(name) >= sizeof(((fl_user_record_t *)0)->name))
        return FL_RESULT_INVAL;
    if (!is_elevated && uid == 0u)
        return FL_RESULT_INVAL;
    for (i = 0; i < db->count; i++) {
        if (db->users[i].uid == uid)
            return FL_RESULT_BUSY;
    }
    if (fl_user_db_find(db, name) || fl_user_db_find_by_key(db, key_id))
        return FL_RESULT_BUSY;
    if (db->count >= FL_USER_DB_MAX_USERS)
        return FL_RESULT_NOMEM;
    u = &db->users[db->count++];
    mem_domain_zero(u, sizeof(*u));
    strncpy(u->name, name, sizeof(u->name) - 1);
    strncpy(u->key_id, key_id, sizeof(u->key_id) - 1);
    if (created_by)
        strncpy(u->created_by, created_by, sizeof(u->created_by) - 1);
    u->created_at = created_at;
    u->uid = uid;
    u->is_elevated = is_elevated ? 1 : 0;
    if (hash_password_fields(password, u->password_hash, sizeof(u->password_hash), u->salt_hex,
                             sizeof(u->salt_hex)) != FL_RESULT_OK ||
        provision_key(u, password) != FL_RESULT_OK) {
        mem_domain_zero(u, sizeof(*u));
        db->count--;
        return FL_RESULT_ERR;
    }
    db->loaded = 1;
    return FL_RESULT_OK;
}

fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated) {
    if (name && strcmp(name, FL_HERITAGE_ROOT_KEY) == 0)
        return add_user_record(db, name, password, uid, is_elevated, FL_HERITAGE_ROOT_KEY, "", 0);
    return fl_user_db_add_user_under(db, FL_HERITAGE_ROOT_KEY, name, password, uid, is_elevated, 0);
}

fl_result_t fl_user_db_add_user_under(fl_user_db_t *db, const char *creator_key, const char *name,
                                      const char *password, uint32_t uid, int is_elevated,
                                      int64_t created_at) {
    char key[FL_HERITAGE_KEY_ID_MAX];
    fl_result_t rc;

    if (!db || !creator_key || !name || !password)
        return FL_RESULT_INVAL;
    if (!fl_heritage_name_available_shape(name))
        return FL_RESULT_INVAL;
    /* Root may be added before anything else exists (seeding); any other creator must be an account. */
    if (strcmp(creator_key, FL_HERITAGE_ROOT_KEY) != 0 && !fl_user_db_find_by_key(db, creator_key))
        return FL_RESULT_NOENT;
    rc = fl_heritage_child(creator_key, name, key, sizeof key);
    if (rc != FL_RESULT_OK)
        return rc;
    return add_user_record(db, name, password, uid, is_elevated, key, creator_key, created_at);
}

fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name) {
    size_t i;
    if (!db || !name)
        return FL_RESULT_INVAL;
    if (strcasecmp(name, FL_HERITAGE_ROOT_KEY) == 0)
        return FL_RESULT_ACCES;
    for (i = 0; i < db->count; i++) {
        if (strcasecmp(db->users[i].name, name) == 0) {
            if (fl_user_db_has_descendants(db, db->users[i].key_id))
                return FL_RESULT_BUSY;
            mem_domain_zero(&db->users[i], sizeof(db->users[i]));
            if (db->count - 1 > i) {
                db->users[i] = db->users[db->count - 1];
                mem_domain_zero(&db->users[db->count - 1], sizeof(db->users[db->count - 1]));
            }
            db->count--;
            if (strcasecmp(db->default_user, name) == 0 && db->count > 0)
                strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}

static fl_user_record_t *find_mut(fl_user_db_t *db, const char *name) {
    size_t i;

    for (i = 0; db && name && i < db->count; i++) {
        if (strcasecmp(db->users[i].name, name) == 0)
            return &db->users[i];
    }
    return NULL;
}

fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password) {
    fl_user_record_t *u;
    fl_user_record_t updated;
    if (!db || !name || !password)
        return FL_RESULT_INVAL;
    u = find_mut(db, name);
    if (!u)
        return FL_RESULT_NOENT;
    /* The old key is sealed under a password the caller does not have: it is replaced. */
    updated = *u;
    if (hash_password_fields(password, updated.password_hash, sizeof(updated.password_hash),
                             updated.salt_hex, sizeof(updated.salt_hex)) != FL_RESULT_OK ||
        provision_key(&updated, password) != FL_RESULT_OK) {
        mem_domain_secure_zero(&updated, sizeof updated);
        return FL_RESULT_ERR;
    }
    *u = updated;
    mem_domain_secure_zero(&updated, sizeof updated);
    return FL_RESULT_OK;
}

fl_result_t fl_user_db_change_password(fl_user_db_t *db, const char *name,
                                       const char *current, const char *password) {
    fl_user_record_t *u;
    fl_user_record_t updated;
    unsigned char secret[FL_USER_SIGNING_SECRET_LEN];
    size_t sealed_len = 0;
    fl_result_t rc = FL_RESULT_OK;
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();

    if (!db || !name || !current || !password)
        return FL_RESULT_INVAL;
    u = find_mut(db, name);
    if (!u)
        return FL_RESULT_NOENT;
    if (!fl_password_verify(current, u->salt_hex, u->password_hash))
        return FL_RESULT_ACCES;
    updated = *u;
    if (hash_password_fields(password, updated.password_hash, sizeof(updated.password_hash),
                             updated.salt_hex, sizeof(updated.salt_hex)) != FL_RESULT_OK)
        rc = FL_RESULT_ERR;
    else if (!u->has_key)
        rc = provision_key(&updated, password);
    else if (!kq ||
             kq->account_open(current, u->signing_sealed, sizeof u->signing_sealed, secret) !=
                 FL_KQ_ACCOUNT_OK ||
             kq->account_seal(password, secret, updated.signing_sealed,
                              sizeof updated.signing_sealed, &sealed_len) != FL_KQ_ACCOUNT_OK ||
             sealed_len != sizeof updated.signing_sealed)
        rc = FL_RESULT_ERR;
    mem_domain_secure_zero(secret, sizeof secret);
    /* The box key moves to the new password the same way, or is created now. */
    if (rc == FL_RESULT_OK && u->has_key && !u->has_box)
        rc = provision_box(&updated, password);
    /* Only a box key carried forward is resealed: without a signing key, provision_key above
     * already made a new box pair, which must not be overwritten with the old secret. */
    else if (rc == FL_RESULT_OK && u->has_key && u->has_box &&
             (!kq ||
              kq->account_open(current, u->box_sealed, sizeof u->box_sealed, secret) !=
                  FL_KQ_ACCOUNT_OK ||
              kq->account_seal(password, secret, updated.box_sealed, sizeof updated.box_sealed,
                               &sealed_len) != FL_KQ_ACCOUNT_OK ||
              sealed_len != sizeof updated.box_sealed))
        rc = FL_RESULT_ERR;
    mem_domain_secure_zero(secret, sizeof secret);
    if (rc == FL_RESULT_OK)
        *u = updated;
    mem_domain_secure_zero(&updated, sizeof updated);
    return rc;
}

fl_result_t fl_user_db_ensure_key(fl_user_db_t *db, const char *name, const char *password,
                                  int *created) {
    fl_user_record_t *u;

    if (created)
        *created = 0;
    if (!db || !name || !password)
        return FL_RESULT_INVAL;
    u = find_mut(db, name);
    if (!u)
        return FL_RESULT_NOENT;
    if (u->has_key && u->has_box)
        return FL_RESULT_OK;
    if (!fl_password_verify(password, u->salt_hex, u->password_hash))
        return FL_RESULT_ACCES;
    /* Accounts from before box keys keep their signing key and gain a box key. */
    if (u->has_key) {
        if (provision_box(u, password) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        if (created)
            *created = FL_USER_DB_CREATED_BOX;
        return FL_RESULT_OK;
    }
    if (provision_key(u, password) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (created)
        *created = FL_USER_DB_CREATED_KEYS;
    return FL_RESULT_OK;
}

void fl_user_db_forget_created(fl_user_db_t *db, const char *name, int created) {
    fl_user_record_t *u;

    if (created == FL_USER_DB_CREATED_KEYS) {
        fl_user_db_forget_key(db, name);
        return;
    }
    if (created != FL_USER_DB_CREATED_BOX)
        return;
    u = find_mut(db, name);
    if (!u)
        return;
    u->has_box = 0;
    mem_domain_zero(u->box_public, sizeof u->box_public);
    mem_domain_zero(u->box_sealed, sizeof u->box_sealed);
}

void fl_user_db_forget_key(fl_user_db_t *db, const char *name) {
    fl_user_record_t *u = find_mut(db, name);

    if (!u)
        return;
    u->has_key = 0;
    mem_domain_zero(u->signing_public, sizeof u->signing_public);
    mem_domain_zero(u->signing_sealed, sizeof u->signing_sealed);
    u->has_box = 0;
    mem_domain_zero(u->box_public, sizeof u->box_public);
    mem_domain_zero(u->box_sealed, sizeof u->box_sealed);
}

/* 1 when secret derives exactly the public key u stores for kind. The one derive-and-
 * compare behind fl_user_db_secret_matches, open_key and open_box: constant time in the
 * secret (asm_mem_ct_equal), and the derived public bytes are wiped on every exit. */
static int record_secret_matches(const fl_user_record_t *u, fl_user_secret_kind_t kind,
                                 const unsigned char *secret) {
    unsigned char derived[FL_USER_SIGNING_PUBLIC_LEN] = {0};
    const unsigned char *stored;
    int derive_rc;
    int ok;
    const fl_kq_driver_t *kq = fl_kq_driver_get_or_register();

    if (!u || !secret)
        return 0;
    if (kind == FL_USER_SECRET_SIGNING) {
        if (!u->has_key)
            return 0;
        stored = u->signing_public;
        derive_rc = kq ? kq->account_public(secret, derived) : FL_KQ_ACCOUNT_INVALID;
    } else if (kind == FL_USER_SECRET_BOX) {
        if (!u->has_box)
            return 0;
        stored = u->box_public;
        derive_rc = kq ? kq->account_box_public(secret, derived) : FL_KQ_ACCOUNT_INVALID;
    } else {
        return 0;
    }
    ok = derive_rc == FL_KQ_ACCOUNT_OK &&
         asm_mem_ct_equal(derived, stored, sizeof derived) != 0;
    mem_domain_secure_zero(derived, sizeof derived);
    return ok;
}

int fl_user_db_secret_matches(const fl_user_db_t *db, const char *key_id,
                              fl_user_secret_kind_t kind, const unsigned char *secret) {
    if (!db || !key_id || !secret)
        return 0;
    return record_secret_matches(fl_user_db_find_by_key(db, key_id), kind, secret);
}

/* Opens the sealed secret of kind for the account name. Once the driver's account_open has been
 * called, every return other than OK leaves secret securely zeroed. */
static fl_result_t open_sealed_record(const fl_user_record_t *u, const char *password,
                                      fl_user_secret_kind_t kind, unsigned char *secret) {
    const unsigned char *sealed;
    size_t sealed_len;
    const fl_kq_driver_t *kq;
    int rc;

    if (!u || !password || !secret)
        return FL_RESULT_INVAL;
    if (kind == FL_USER_SECRET_SIGNING && u->has_key) {
        sealed = (const unsigned char *)u->signing_sealed;
        sealed_len = sizeof u->signing_sealed;
    } else if (kind == FL_USER_SECRET_BOX && u->has_box) {
        sealed = (const unsigned char *)u->box_sealed;
        sealed_len = sizeof u->box_sealed;
    } else {
        return FL_RESULT_NOENT;
    }
    kq = fl_kq_driver_get_or_register();
    if (!kq)
        return FL_RESULT_ERR;
    rc = kq->account_open(password, sealed, sealed_len, secret);
    if (rc != FL_KQ_ACCOUNT_OK) {
        mem_domain_secure_zero(secret, FL_USER_SIGNING_SECRET_LEN);
        return rc == FL_KQ_ACCOUNT_WRONG_PASSWORD ? FL_RESULT_ACCES : FL_RESULT_ERR;
    }
    /* The sealed secret must be the one the stored public key names. */
    if (!record_secret_matches(u, kind, secret)) {
        mem_domain_secure_zero(secret, FL_USER_SIGNING_SECRET_LEN);
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static fl_result_t open_sealed(const fl_user_db_t *db, const char *name, const char *password,
                               fl_user_secret_kind_t kind, unsigned char *secret) {
    const fl_user_record_t *u;

    if (!db || !name || !password || !secret)
        return FL_RESULT_INVAL;
    u = fl_user_db_find(db, name);
    if (!u)
        return FL_RESULT_NOENT;
    return open_sealed_record(u, password, kind, secret);
}

fl_result_t fl_user_db_open_key_record(const fl_user_record_t *account, const char *password,
                                      unsigned char *secret) {
    return open_sealed_record(account, password, FL_USER_SECRET_SIGNING, secret);
}

fl_result_t fl_user_db_open_key(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret) {
    return open_sealed(db, name, password, FL_USER_SECRET_SIGNING, secret);
}

const unsigned char *fl_user_db_signing_public(const fl_user_db_t *db, const char *key_id) {
    const fl_user_record_t *u = fl_user_db_find_by_key(db, key_id);

    return (u && u->has_key) ? u->signing_public : NULL;
}

fl_result_t fl_user_db_open_box(const fl_user_db_t *db, const char *name, const char *password,
                                unsigned char *secret) {
    return open_sealed(db, name, password, FL_USER_SECRET_BOX, secret);
}

const unsigned char *fl_user_db_box_public(const fl_user_db_t *db, const char *key_id) {
    const fl_user_record_t *u = fl_user_db_find_by_key(db, key_id);

    return (u && u->has_box) ? u->box_public : NULL;
}

fl_result_t fl_user_db_set_auth_mode(fl_user_db_t *db, const char *name, int auth_mode) {
    fl_user_record_t *u;

    if (!db || !name || (auth_mode != FL_USER_AUTH_PASSWORD && auth_mode != FL_USER_AUTH_PASSWORD_KQST))
        return FL_RESULT_INVAL;
    u = (fl_user_record_t *)fl_user_db_find(db, name);
    if (!u)
        return FL_RESULT_NOENT;
    u->auth_mode = auth_mode;
    return FL_RESULT_OK;
}

static fl_result_t kqst_path_select(sqlite3 *sql, const char *key_id, char *out, size_t cap) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (sqlite3_prepare_v2(sql, "SELECT kqst_path FROM account_keys WHERE label = ?;", -1, &st, NULL) !=
        SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, key_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const char *v = (const char *)sqlite3_column_text(st, 0);

        if (v && v[0]) {
            if (strlen(v) >= cap) {
                rc = FL_RESULT_NOMEM;
            } else {
                memcpy(out, v, strlen(v) + 1);
                rc = FL_RESULT_OK;
            }
        }
    }
    sqlite3_finalize(st);
    return rc;
}

fl_result_t fl_user_db_kqst_path(const fl_user_db_t *db, const char *key_id, char *out, size_t cap) {
    sqlite3 *sql = NULL;
    fl_result_t rc;

    if (!db || !key_id || !out || cap == 0)
        return FL_RESULT_INVAL;
    out[0] = '\0';
    if (sqlite3_open_v2(db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH, &sql, SQLITE_OPEN_READONLY, NULL) !=
        SQLITE_OK) {
        if (sql)
            sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    rc = kqst_path_select(sql, key_id, out, cap);
    sqlite3_close(sql);
    return rc;
}

static fl_result_t kqst_pairing_select(sqlite3 *sql, const char *key_id, unsigned char device_id[16],
                                       unsigned char signing_public[32],
                                       unsigned char encryption_public[32]) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (sqlite3_prepare_v2(sql,
                           "SELECT device_id, signing_public, encryption_public FROM account_keys "
                           "WHERE label = ?;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, key_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        const void *dev = sqlite3_column_blob(st, 0);
        const void *sig = sqlite3_column_blob(st, 1);
        const void *enc = sqlite3_column_blob(st, 2);

        if (dev && sig && enc && sqlite3_column_bytes(st, 0) == 16 &&
            sqlite3_column_bytes(st, 1) == 32 && sqlite3_column_bytes(st, 2) == 32) {
            memcpy(device_id, dev, 16);
            memcpy(signing_public, sig, 32);
            memcpy(encryption_public, enc, 32);
            rc = FL_RESULT_OK;
        }
    }
    sqlite3_finalize(st);
    return rc;
}

fl_result_t fl_user_db_kqst_pairing(const fl_user_db_t *db, const char *key_id,
                                    unsigned char device_id[16], unsigned char signing_public[32],
                                    unsigned char encryption_public[32]) {
    sqlite3 *sql = NULL;
    fl_result_t rc;

    if (!db || !key_id || !device_id || !signing_public || !encryption_public)
        return FL_RESULT_INVAL;
    if (sqlite3_open_v2(db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH, &sql, SQLITE_OPEN_READONLY, NULL) !=
        SQLITE_OK) {
        if (sql)
            sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    rc = kqst_pairing_select(sql, key_id, device_id, signing_public, encryption_public);
    sqlite3_close(sql);
    return rc;
}

/* ---- Private bridges (account_bridges, account_bridge_members) ---- */

static fl_result_t bridge_db_open(const fl_user_db_t *db, int writable, sqlite3 **out) {
    const char *path = db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH;

    *out = NULL;
    if (sqlite3_open_v2(path, out, writable ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY,
                        NULL) != SQLITE_OK) {
        sqlite3_close(*out);
        *out = NULL;
        return FL_RESULT_ERR;
    }
    /* A writer brings an older store's tables up to date first (the wrap_salt column). */
    if (writable && db_init_schema(*out) != FL_RESULT_OK) {
        sqlite3_close(*out);
        *out = NULL;
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static int blob_is(sqlite3_stmt *st, int col, size_t len) {
    return sqlite3_column_type(st, col) == SQLITE_BLOB && sqlite3_column_bytes(st, col) == (int)len &&
           sqlite3_column_blob(st, col) != NULL;
}

/* Reads uid's row and its members into out. NOENT for no row or a malformed one. */
static fl_result_t bridge_read(sqlite3 *sql, const char *uid, fl_user_bridge_t *out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;
    sqlite3_int64 gen;

    mem_domain_zero(out, sizeof *out);
    if (strlen(uid) >= sizeof out->uid)
        return FL_RESULT_NOENT;
    if (sqlite3_prepare_v2(sql,
                           "SELECT generation, salt, public_key, destroyed FROM account_bridges WHERE uid = ?;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, uid, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW) {
        gen = sqlite3_column_int64(st, 0);
        if (gen >= 1 && gen <= (sqlite3_int64)UINT32_MAX && blob_is(st, 1, FL_USER_BRIDGE_SALT_LEN) &&
            blob_is(st, 2, 32)) {
            memcpy(out->uid, uid, strlen(uid) + 1);
            out->generation = (uint32_t)gen;
            memcpy(out->salt, sqlite3_column_blob(st, 1), FL_USER_BRIDGE_SALT_LEN);
            memcpy(out->public_key, sqlite3_column_blob(st, 2), 32);
            out->destroyed = sqlite3_column_int(st, 3) != 0;
            rc = FL_RESULT_OK;
        }
    }
    sqlite3_finalize(st);
    if (rc != FL_RESULT_OK)
        return rc;
    if (sqlite3_prepare_v2(sql,
                           "SELECT label, encryption_public, signing_public FROM account_bridge_members "
                           "WHERE uid = ? ORDER BY label;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, uid, -1, SQLITE_TRANSIENT);
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *label = (const char *)sqlite3_column_text(st, 0);
        fl_user_bridge_member_t *m;

        /* A roster the store cannot hold whole, or a member it cannot read, is no roster. */
        if (out->nmembers >= FL_USER_BRIDGE_MEMBERS_MAX || !label || !fl_heritage_key_valid(label) ||
            strlen(label) >= sizeof out->members[0].label || !blob_is(st, 1, 32) || !blob_is(st, 2, 32)) {
            rc = FL_RESULT_NOENT;
            break;
        }
        m = &out->members[out->nmembers++];
        memcpy(m->label, label, strlen(label) + 1);
        memcpy(m->encryption_public, sqlite3_column_blob(st, 1), 32);
        memcpy(m->signing_public, sqlite3_column_blob(st, 2), 32);
    }
    sqlite3_finalize(st);
    if (rc == FL_RESULT_OK && out->nmembers < 2)
        rc = FL_RESULT_NOENT;
    if (rc != FL_RESULT_OK)
        mem_domain_zero(out, sizeof *out);
    return rc;
}

fl_result_t fl_user_db_bridge_get(const fl_user_db_t *db, const char *uid, fl_user_bridge_t *out) {
    sqlite3 *sql = NULL;
    fl_result_t rc;

    if (!db || !uid || !uid[0] || !out)
        return FL_RESULT_INVAL;
    if (bridge_db_open(db, 0, &sql) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    rc = bridge_read(sql, uid, out);
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_user_db_bridges_live(const fl_user_db_t *db, fl_user_bridge_t *out, size_t cap,
                                    size_t *count) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_OK;

    if (!db || !count || (cap && !out))
        return FL_RESULT_INVAL;
    *count = 0;
    if (bridge_db_open(db, 0, &sql) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(sql, "SELECT uid FROM account_bridges WHERE destroyed = 0 ORDER BY uid;", -1,
                           &st, NULL) != SQLITE_OK) {
        /* A store written before the bridge tables has none: no live bridge. */
        sqlite3_close(sql);
        return FL_RESULT_OK;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *uid = (const char *)sqlite3_column_text(st, 0);
        fl_result_t one;

        if (!uid)
            continue;
        if (*count >= cap) {
            rc = FL_RESULT_NOMEM; /* more live bridges than the caller holds: never a partial list */
            break;
        }
        one = bridge_read(sql, uid, &out[*count]);
        if (one == FL_RESULT_OK && !out[*count].destroyed)
            (*count)++;
        else if (one == FL_RESULT_ERR) {
            rc = one;
            break;
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(sql);
    if (rc != FL_RESULT_OK)
        *count = 0;
    return rc;
}

fl_result_t fl_user_db_bridge_put(const fl_user_db_t *db, const fl_user_bridge_t *b) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_OK;
    size_t i;

    if (!db || !b || !b->uid[0] || memchr(b->uid, '\0', sizeof b->uid) == NULL || b->generation == 0 ||
        b->nmembers < 2 || b->nmembers > FL_USER_BRIDGE_MEMBERS_MAX)
        return FL_RESULT_INVAL;
    for (i = 0; i < b->nmembers; i++)
        if (!fl_heritage_key_valid(b->members[i].label))
            return FL_RESULT_INVAL;
    if (bridge_db_open(db, 1, &sql) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_sqlite_exec(sql, "user_db", "BEGIN IMMEDIATE;") != FL_RESULT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    if (sqlite3_prepare_v2(sql,
                           "INSERT INTO account_bridges (uid, generation, salt, public_key, destroyed) "
                           "VALUES (?,?,?,?,?) ON CONFLICT(uid) DO UPDATE SET generation=excluded.generation,"
                           " salt=excluded.salt, public_key=excluded.public_key, destroyed=excluded.destroyed;",
                           -1, &st, NULL) != SQLITE_OK) {
        rc = FL_RESULT_ERR;
    } else {
        sqlite3_bind_text(st, 1, b->uid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, (sqlite3_int64)b->generation);
        sqlite3_bind_blob(st, 3, b->salt, FL_USER_BRIDGE_SALT_LEN, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 4, b->public_key, 32, SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 5, b->destroyed ? 1 : 0);
        if (sqlite3_step(st) != SQLITE_DONE)
            rc = FL_RESULT_ERR;
        sqlite3_finalize(st);
    }
    /* Members no longer on the roster leave, with their sealed keys. */
    if (rc == FL_RESULT_OK) {
        if (sqlite3_prepare_v2(sql, "DELETE FROM account_bridge_members WHERE uid = ?;", -1, &st, NULL) !=
            SQLITE_OK) {
            rc = FL_RESULT_ERR;
        } else {
            sqlite3_bind_text(st, 1, b->uid, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(st) != SQLITE_DONE)
                rc = FL_RESULT_ERR;
            sqlite3_finalize(st);
        }
    }
    for (i = 0; rc == FL_RESULT_OK && i < b->nmembers; i++) {
        if (sqlite3_prepare_v2(sql,
                               "INSERT INTO account_bridge_members (uid, label, encryption_public, signing_public) "
                               "VALUES (?,?,?,?);",
                               -1, &st, NULL) != SQLITE_OK) {
            rc = FL_RESULT_ERR;
            break;
        }
        sqlite3_bind_text(st, 1, b->uid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 2, b->members[i].label, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 3, b->members[i].encryption_public, 32, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 4, b->members[i].signing_public, 32, SQLITE_TRANSIENT);
        if (sqlite3_step(st) != SQLITE_DONE)
            rc = FL_RESULT_ERR;
        sqlite3_finalize(st);
    }
    if (rc == FL_RESULT_OK && fl_sqlite_exec(sql, "user_db", "COMMIT;") != FL_RESULT_OK)
        rc = FL_RESULT_ERR;
    if (rc != FL_RESULT_OK)
        (void)fl_sqlite_exec(sql, "user_db", "ROLLBACK;");
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_user_db_bridge_put_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                             const unsigned char *sealed, size_t len,
                                             const unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!db || !uid || !label || !sealed || !wrap_salt || len == 0 || len > FL_USER_BRIDGE_SEALED_MAX)
        return FL_RESULT_INVAL;
    if (bridge_db_open(db, 1, &sql) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    /* One statement: the sealed key and its wrap salt are never stored apart. */
    if (sqlite3_prepare_v2(sql,
                           "UPDATE account_bridge_members SET sealed_bridge_key = ?, wrap_salt = ? "
                           "WHERE uid = ? AND label = ?;",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, sealed, (int)len, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 2, wrap_salt, FL_USER_BRIDGE_SALT_LEN, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 3, uid, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, label, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_DONE)
            rc = sqlite3_changes(sql) == 1 ? FL_RESULT_OK : FL_RESULT_NOENT;
        sqlite3_finalize(st);
    }
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_user_db_bridge_sealed_key(const fl_user_db_t *db, const char *uid, const char *label,
                                         unsigned char *sealed, size_t cap, size_t *len,
                                         unsigned char wrap_salt[FL_USER_BRIDGE_SALT_LEN]) {
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (!db || !uid || !label || !sealed || !len || !wrap_salt)
        return FL_RESULT_INVAL;
    *len = 0;
    if (bridge_db_open(db, 0, &sql) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(sql,
                           "SELECT sealed_bridge_key, wrap_salt FROM account_bridge_members "
                           "WHERE uid = ? AND label = ?;",
                           -1, &st, NULL) != SQLITE_OK) {
        /* A store from before wrap_salt holds no sealed key that could open. */
        sqlite3_close(sql);
        return FL_RESULT_NOENT;
    }
    sqlite3_bind_text(st, 1, uid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, label, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) == SQLITE_BLOB &&
        blob_is(st, 1, FL_USER_BRIDGE_SALT_LEN)) {
        int n = sqlite3_column_bytes(st, 0);

        if (n <= 0 || (size_t)n > FL_USER_BRIDGE_SEALED_MAX) {
            rc = FL_RESULT_NOENT;
        } else if ((size_t)n > cap) {
            rc = FL_RESULT_NOMEM;
        } else {
            memcpy(sealed, sqlite3_column_blob(st, 0), (size_t)n);
            memcpy(wrap_salt, sqlite3_column_blob(st, 1), FL_USER_BRIDGE_SALT_LEN);
            *len = (size_t)n;
            rc = FL_RESULT_OK;
        }
    }
    sqlite3_finalize(st);
    sqlite3_close(sql);
    return rc;
}
