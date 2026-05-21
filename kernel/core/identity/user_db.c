#include "user_db.h"
#include "password_hash.h"
#include "mem_domain.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static fl_result_t db_exec(sqlite3 *sql, const char *stmt) {
    char *err = NULL;
    if (sqlite3_exec(sql, stmt, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "user_db: %s\n", err);
            sqlite3_free(err);
        }
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static fl_result_t db_init_schema(sqlite3 *sql) {
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS users ("
        "  name TEXT PRIMARY KEY NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  salt_hex TEXT NOT NULL,"
        "  uid INTEGER NOT NULL,"
        "  is_elevated INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS meta ("
        "  key TEXT PRIMARY KEY NOT NULL,"
        "  value TEXT NOT NULL"
        ");";
    return db_exec(sql, schema);
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
    char stmt[128];
    snprintf(stmt, sizeof(stmt),
             "INSERT INTO meta(key,value) VALUES('default_user','%s') "
             "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
             user);
    return db_exec(sql, stmt);
}

static fl_result_t hash_password_fields(const char *password, char *hash_hex, size_t hash_sz,
                                        char *salt_hex, size_t salt_sz) {
    if (fl_password_generate_salt_hex(salt_hex, salt_sz) != 0)
        return FL_RESULT_ERR;
    if (fl_password_hash_password(password, salt_hex, hash_hex, hash_sz) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t db_upsert_user(sqlite3 *sql, const fl_user_record_t *u) {
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO users(name,password_hash,salt_hex,uid,is_elevated) "
        "VALUES(?,?,?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET password_hash=excluded.password_hash,"
        "salt_hex=excluded.salt_hex,uid=excluded.uid,is_elevated=excluded.is_elevated;";
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, u->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, u->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, u->salt_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, (int)u->uid);
    sqlite3_bind_int(st, 5, u->is_elevated ? 1 : 0);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

static fl_result_t db_load_users(sqlite3 *sql, fl_user_db_t *db) {
    sqlite3_stmt *st = NULL;
    const char *q = "SELECT name,password_hash,salt_hex,uid,is_elevated FROM users ORDER BY uid;";
    if (sqlite3_prepare_v2(sql, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    db->count = 0;
    while (sqlite3_step(st) == SQLITE_ROW && db->count < FL_USER_DB_MAX_USERS) {
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
        db->count++;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

void fl_user_db_seed_defaults(fl_user_db_t *db) {
    if (!db)
        return;
    fl_user_db_clear(db);
    strncpy(db->default_user, "flinstone", sizeof(db->default_user) - 1);
    fl_user_db_add_user(db, "flinstone", "flinstone", 1000, 0);
    fl_user_db_add_user(db, "root", "root", 0, 1);
    db->loaded = 1;
}

fl_result_t fl_user_db_load(fl_user_db_t *db, const char *path) {
    sqlite3 *sql = NULL;
    fl_result_t rc;

    if (!db || !path)
        return FL_RESULT_INVAL;
    fl_user_db_clear(db);
    strncpy(db->path, path, sizeof(db->path) - 1);

    if (sqlite3_open(path, &sql) != SQLITE_OK)
        return FL_RESULT_NOENT;
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
        fl_user_record_t flin, root;
        mem_domain_zero(&flin, sizeof(flin));
        mem_domain_zero(&root, sizeof(root));
        strncpy(flin.name, "flinstone", sizeof(flin.name) - 1);
        strncpy(root.name, "root", sizeof(root.name) - 1);
        flin.uid = 1000;
        root.uid = 0;
        root.is_elevated = 1;
        if (hash_password_fields("flinstone", flin.password_hash, sizeof(flin.password_hash),
                                 flin.salt_hex, sizeof(flin.salt_hex)) != FL_RESULT_OK
            || hash_password_fields("root", root.password_hash, sizeof(root.password_hash),
                                    root.salt_hex, sizeof(root.salt_hex)) != FL_RESULT_OK) {
            sqlite3_close(sql);
            return FL_RESULT_ERR;
        }
        if (db_upsert_user(sql, &flin) != FL_RESULT_OK || db_upsert_user(sql, &root) != FL_RESULT_OK) {
            sqlite3_close(sql);
            return FL_RESULT_ERR;
        }
        (void)db_write_meta_default(sql, "flinstone");
        rc = db_load_users(sql, db);
    }

    (void)db_read_meta_default(sql, db->default_user, sizeof(db->default_user));
    if (!db->default_user[0] && db->count > 0)
        strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);

    db->loaded = 1;
    sqlite3_close(sql);
    return rc;
}

fl_result_t fl_user_db_save(const fl_user_db_t *db) {
    sqlite3 *sql = NULL;
    size_t i;
    const char *path;
    fl_result_t rc = FL_RESULT_OK;

    if (!db || !db->loaded)
        return FL_RESULT_INVAL;
    path = db->path[0] ? db->path : FL_USERS_DB_DEFAULT_PATH;
    if (sqlite3_open(path, &sql) != SQLITE_OK)
        return FL_RESULT_ERR;
    if (db_init_schema(sql) != FL_RESULT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    if (db_exec(sql, "DELETE FROM users;") != FL_RESULT_OK) {
        sqlite3_close(sql);
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
        if (strcmp(db->users[i].name, name) == 0)
            return &db->users[i];
    }
    return NULL;
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

fl_result_t fl_user_db_add_user(fl_user_db_t *db, const char *name, const char *password,
                                uint32_t uid, int is_elevated) {
    fl_user_record_t *u;
    if (!db || !name || !name[0] || !password)
        return FL_RESULT_INVAL;
    if (fl_user_db_find(db, name))
        return FL_RESULT_BUSY;
    if (db->count >= FL_USER_DB_MAX_USERS)
        return FL_RESULT_NOMEM;
    u = &db->users[db->count++];
    mem_domain_zero(u, sizeof(*u));
    strncpy(u->name, name, sizeof(u->name) - 1);
    u->uid = uid;
    u->is_elevated = is_elevated ? 1 : 0;
    if (hash_password_fields(password, u->password_hash, sizeof(u->password_hash), u->salt_hex,
                             sizeof(u->salt_hex)) != FL_RESULT_OK) {
        db->count--;
        return FL_RESULT_ERR;
    }
    db->loaded = 1;
    return FL_RESULT_OK;
}

fl_result_t fl_user_db_remove_user(fl_user_db_t *db, const char *name) {
    size_t i;
    if (!db || !name)
        return FL_RESULT_INVAL;
    if (strcmp(name, "root") == 0)
        return FL_RESULT_ACCES;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].name, name) == 0) {
            if (db->count - 1 > i)
                db->users[i] = db->users[db->count - 1];
            db->count--;
            if (strcmp(db->default_user, name) == 0 && db->count > 0)
                strncpy(db->default_user, db->users[0].name, sizeof(db->default_user) - 1);
            return FL_RESULT_OK;
        }
    }
    return FL_RESULT_NOENT;
}

fl_result_t fl_user_db_set_password(fl_user_db_t *db, const char *name, const char *password) {
    fl_user_record_t *u;
    size_t i;
    if (!db || !name || !password)
        return FL_RESULT_INVAL;
    for (i = 0; i < db->count; i++) {
        if (strcmp(db->users[i].name, name) == 0) {
            u = &db->users[i];
            return hash_password_fields(password, u->password_hash, sizeof(u->password_hash),
                                        u->salt_hex, sizeof(u->salt_hex));
        }
    }
    return FL_RESULT_NOENT;
}
