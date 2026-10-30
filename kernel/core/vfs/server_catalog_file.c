/*
 * The catalog file (server_shared/host_catalog.db) itself: creating it owner-only, opening it,
 * the schema and its version stamp, and the event history. Two modules keep their own connection
 * to one file through this: the share path (server_shared_db.c) and the KeyQuorum delivery store
 * (kq_delivery.c). Neither sees the other's rows: the share path reads FILE and MSG rows only and
 * the store reads FL_CHANNEL_PAYLOAD_KQ rows only. Each unit of work is one SQLite transaction
 * (BEGIN IMMEDIATE) and a busy connection waits FL_SERVER_CATALOG_BUSY_TIMEOUT_MS.
 */
#include "server_shared_db.h"
#include "fl/sqlite_schema.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

fl_result_t fl_catalog_file_user_version(sqlite3 *sql, int *out)
{
    sqlite3_stmt *st = NULL;
    *out = 0;
    if (sqlite3_prepare_v2(sql, "PRAGMA user_version;", -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    *out = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

/* Applies the schema and the version stamp. A file from a newer build (or a negative, invalid
 * value) is refused. A file with no stamp (written before this check) is the same schema and is
 * stamped, after the schema exists, in one transaction so a failure leaves the file as it was. */
static fl_result_t db_init_schema(sqlite3 *sql);

static fl_result_t db_apply_schema_and_version(sqlite3 *sql)
{
    int v = 0;
    char stmt[48];
    if (fl_catalog_file_user_version(sql, &v) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (v < 0 || v > FL_SERVER_CATALOG_SCHEMA_VERSION) {
        fprintf(stderr, "server_catalog: schema version %d is not supported (this build: %d)\n",
                v, FL_SERVER_CATALOG_SCHEMA_VERSION);
        return FL_RESULT_ERR;
    }
    if (v == FL_SERVER_CATALOG_SCHEMA_VERSION)
        return db_init_schema(sql);
    if (fl_sqlite_exec(sql, "server_catalog", "BEGIN IMMEDIATE;") != FL_RESULT_OK)
        return FL_RESULT_ERR;
    /* Re-read under the write lock: another process may have stamped it meanwhile. */
    if (fl_catalog_file_user_version(sql, &v) != FL_RESULT_OK || v < 0 ||
        v > FL_SERVER_CATALOG_SCHEMA_VERSION || db_init_schema(sql) != FL_RESULT_OK) {
        (void)fl_sqlite_exec(sql, "server_catalog", "ROLLBACK;");
        return FL_RESULT_ERR;
    }
    if (v < FL_SERVER_CATALOG_SCHEMA_VERSION) {
        snprintf(stmt, sizeof(stmt), "PRAGMA user_version=%d;", FL_SERVER_CATALOG_SCHEMA_VERSION);
        if (fl_sqlite_exec(sql, "server_catalog", stmt) != FL_RESULT_OK) {
            (void)fl_sqlite_exec(sql, "server_catalog", "ROLLBACK;");
            return FL_RESULT_ERR;
        }
    }
    return fl_sqlite_exec(sql, "server_catalog", "COMMIT;");
}

static fl_result_t db_init_schema(sqlite3 *sql)
{
    static const char *schema =
        "CREATE TABLE IF NOT EXISTS catalog_entry ("
        "  transfer_id TEXT PRIMARY KEY NOT NULL,"
        "  content_hash TEXT NOT NULL,"
        "  payload_kind INTEGER NOT NULL,"
        "  sender_member_id INTEGER NOT NULL,"
        "  receiver_member_id INTEGER NOT NULL DEFAULT 0,"
        "  file_perms INTEGER NOT NULL DEFAULT 0,"
        "  is_public INTEGER NOT NULL DEFAULT 0,"
        "  created_at INTEGER NOT NULL,"
        "  expires_at INTEGER NOT NULL DEFAULT 0,"
        "  payload_name TEXT NOT NULL,"
        "  storage_relpath TEXT NOT NULL,"
        "  status INTEGER NOT NULL DEFAULT 0,"
        "  total_bytes INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE TABLE IF NOT EXISTS catalog_acl ("
        "  transfer_id TEXT NOT NULL,"
        "  member_id INTEGER NOT NULL,"
        "  PRIMARY KEY (transfer_id, member_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS catalog_event ("
        "  transfer_id TEXT NOT NULL,"
        "  seq INTEGER NOT NULL,"
        "  event TEXT NOT NULL,"
        "  actor_member_id INTEGER NOT NULL,"
        "  at INTEGER NOT NULL,"
        "  detail TEXT NOT NULL DEFAULT '',"
        "  PRIMARY KEY (transfer_id, seq)"
        ");"
        /* The KeyQuorum part of a FL_CHANNEL_PAYLOAD_KQ row: account key ids and the signed
         * offer do not fit the member-id columns (kq_delivery.c owns these rows). */
        "CREATE TABLE IF NOT EXISTS catalog_kq ("
        "  transfer_id TEXT PRIMARY KEY NOT NULL,"
        "  delivery_id BLOB NOT NULL UNIQUE,"
        "  sender_key TEXT NOT NULL,"
        "  recipient_key TEXT NOT NULL,"
        "  offer BLOB NOT NULL,"
        "  cosig BLOB,"
        "  released_at INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_catalog_kq_recipient ON catalog_kq(recipient_key);"
        "CREATE INDEX IF NOT EXISTS idx_catalog_transfer ON catalog_entry(transfer_id);"
        "CREATE INDEX IF NOT EXISTS idx_catalog_hash ON catalog_entry(content_hash);"
        "CREATE INDEX IF NOT EXISTS idx_catalog_receiver ON catalog_entry(receiver_member_id);";
    return fl_sqlite_exec(sql, "server_catalog", schema);
}

/* Makes sure the catalog file exists and is private where we can make it so. A missing file is
 * created owner-only (never through a link) before SQLite sees it, so it is never wider and its
 * journal files, which SQLite creates with the same mode, are not either. An existing file is
 * never followed through a link and must be a regular file with one name. A loose mode is
 * tightened when the file is ours; a file we cannot tighten (another owner, a read-only
 * medium) is still opened, with a warning, because refusing would make a readable catalog
 * unreadable: only a newer schema is refused. */
static fl_result_t catalog_prepare_file(const char *path)
{
    struct stat sb;
    int fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                  FL_SERVER_CATALOG_FILE_MODE);
    if (fd >= 0) {
        close(fd);
        return FL_RESULT_OK;
    }
    if (errno != EEXIST) {
        fprintf(stderr, "server_catalog: cannot create the catalog file (errno %d)\n", errno);
        return FL_RESULT_ERR;
    }
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "server_catalog: cannot open the catalog file (errno %d)\n", errno);
        return FL_RESULT_ERR;
    }
    if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_nlink != 1) {
        fprintf(stderr, "server_catalog: the catalog file is not a regular file with one name\n");
        close(fd);
        return FL_RESULT_ERR;
    }
    /* As KeyQuorum's database open does (src/db/mod.rs, restrict_owner_only): the file is set to
     * owner-only (0600) on every open, and an open fails when that cannot be done, rather than
     * reading a catalog other users can read or keeping a looser mode. */
    if (fchmod(fd, FL_SERVER_CATALOG_FILE_MODE) != 0) {
        fprintf(stderr, "server_catalog: the catalog file is accessible to others and cannot be "
                        "made owner-only\n");
        close(fd);
        return FL_RESULT_ERR;
    }
    close(fd);
    return FL_RESULT_OK;
}

static int catalog_sqlite_open(const char *path, sqlite3 **out)
{
    /* No CREATE: catalog_prepare_file made the file. A write-protected file opens read-only. */
    int flags = SQLITE_OPEN_READWRITE;
#ifdef SQLITE_OPEN_NOFOLLOW
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
    return sqlite3_open_v2(path, out, flags, NULL);
}

/* A catalog we cannot write is only read: no schema, no stamp, nothing is changed. */
static fl_result_t db_check_read_only(sqlite3 *sql)
{
    int v = 0;
    if (fl_catalog_file_user_version(sql, &v) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (v < 0 || v > FL_SERVER_CATALOG_SCHEMA_VERSION) {
        fprintf(stderr, "server_catalog: schema version %d is not supported (this build: %d)\n",
                v, FL_SERVER_CATALOG_SCHEMA_VERSION);
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}


fl_result_t fl_catalog_file_open(const char *db_path, sqlite3 **out)
{
    sqlite3 *sql = NULL;

    if (!db_path || !out)
        return FL_RESULT_INVAL;
    *out = NULL;
    if (catalog_prepare_file(db_path) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (catalog_sqlite_open(db_path, &sql) != SQLITE_OK) {
        if (sql)
            sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    sqlite3_busy_timeout(sql, FL_SERVER_CATALOG_BUSY_TIMEOUT_MS);
    if ((sqlite3_db_readonly(sql, "main") == 1 ? db_check_read_only(sql)
                                               : db_apply_schema_and_version(sql)) !=
        FL_RESULT_OK) {
        sqlite3_close(sql);
        return FL_RESULT_ERR;
    }
    *out = sql;
    return FL_RESULT_OK;
}

/* Which rows a history may be written for: the share path writes for an id that is not a row of
 * another kind (its event can come before its row), the store only for its own KQ row. */
static const char *event_guard(int kq)
{
    return kq ? "EXISTS (SELECT 1 FROM catalog_entry c WHERE c.transfer_id=?7 AND "
                "c.payload_kind=3)"
              : "NOT EXISTS (SELECT 1 FROM catalog_entry c WHERE c.transfer_id=?7 AND NOT "
                FL_CATALOG_SHAREABLE_KINDS_SQL_C ")";
}

fl_result_t fl_catalog_file_append_event(sqlite3 *sql, int kq, const char *transfer_id,
                                         const char *event, uint16_t actor_member_id,
                                         uint64_t at, const char *detail)
{
    char ins[512];
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_OK;

    if (!sql || !transfer_id || !transfer_id[0] || !event || !event[0])
        return FL_RESULT_INVAL;
    /* The next seq is computed in the same statement, so one id never gets two of a number. */
    if (snprintf(ins, sizeof ins,
                 "INSERT INTO catalog_event(transfer_id,seq,event,actor_member_id,at,detail) "
                 "SELECT ?1,(SELECT COALESCE(MAX(seq),0)+1 FROM catalog_event "
                 "WHERE transfer_id=?1),?3,?4,?5,?6 WHERE %s;",
                 event_guard(kq)) >= (int)sizeof ins)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, event, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, (int)actor_member_id);
    sqlite3_bind_int64(st, 5, (sqlite3_int64)at);
    sqlite3_bind_text(st, 6, detail ? detail : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, transfer_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE)
        rc = FL_RESULT_ERR;
    else if (sqlite3_changes(sql) == 0)
        rc = FL_RESULT_ACCES; /* the id belongs to a row of a kind this history does not hold */
    sqlite3_finalize(st);
    return rc;
}

fl_result_t fl_catalog_file_list_events(sqlite3 *sql, int kq, const char *transfer_id,
                                        fl_server_catalog_event_t *out, size_t out_cap,
                                        size_t *out_count)
{
    char sel[512];
    sqlite3_stmt *st = NULL;
    size_t n = 0;

    if (out_count)
        *out_count = 0;
    if (!sql || !transfer_id || !transfer_id[0] || !out || !out_count || out_cap == 0u)
        return FL_RESULT_INVAL;
    if (snprintf(sel, sizeof sel,
                 "SELECT seq,event,actor_member_id,at,detail FROM catalog_event "
                 "WHERE transfer_id=?7 AND %s ORDER BY seq ASC LIMIT ?2;",
                 event_guard(kq)) >= (int)sizeof sel)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(sql, sel, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 7, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, (sqlite3_int64)out_cap);
    while (n < out_cap && sqlite3_step(st) == SQLITE_ROW) {
        fl_server_catalog_event_t *e = &out[n];
        const unsigned char *txt;
        memset(e, 0, sizeof(*e));
        e->seq = (uint32_t)sqlite3_column_int64(st, 0);
        txt = sqlite3_column_text(st, 1);
        if (txt)
            snprintf(e->event, sizeof(e->event), "%s", (const char *)txt);
        e->actor_member_id = (uint16_t)sqlite3_column_int(st, 2);
        e->at = (uint64_t)sqlite3_column_int64(st, 3);
        txt = sqlite3_column_text(st, 4);
        if (txt)
            snprintf(e->detail, sizeof(e->detail), "%s", (const char *)txt);
        n++;
    }
    sqlite3_finalize(st);
    *out_count = n;
    return FL_RESULT_OK;
}
