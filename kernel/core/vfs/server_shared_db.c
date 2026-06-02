#include "server_shared_db.h"
#include "server_shared_digest.h"

#include "common.h"
#include "contract_p5_server_share.h"

#include <errno.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static sqlite3 *s_catalog_db;
static uint32_t s_msg_serial;

static fl_result_t db_exec(sqlite3 *sql, const char *stmt)
{
    char *err = NULL;
    if (sqlite3_exec(sql, stmt, NULL, NULL, &err) != SQLITE_OK) {
        if (err) {
            fprintf(stderr, "server_catalog: %s\n", err);
            sqlite3_free(err);
        }
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

static int join_path(char *out, size_t cap, const char *a, const char *b)
{
    int r;
    if (!out || cap == 0u)
        return -1;
    if (!a || !a[0])
        r = snprintf(out, cap, "%s", b ? b : "");
    else if (a[strlen(a) - 1] == '/')
        r = snprintf(out, cap, "%s%s", a, b ? b : "");
    else
        r = snprintf(out, cap, "%s/%s", a, b ? b : "");
    if (r < 0 || (size_t)r >= cap)
        return -1;
    return 0;
}

static fl_result_t catalog_db_path(char *out, size_t cap)
{
    char root[512];
    if (join_path(root, sizeof(root), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (join_path(out, cap, root, FL_SERVER_CATALOG_DB_NAME) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t catalog_blobs_root(char *out, size_t cap)
{
    char root[512];
    if (join_path(root, sizeof(root), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (join_path(out, cap, root, FL_SERVER_CATALOG_BLOBS_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t mkdir_p_simple(const char *path)
{
    char tmp[512];
    size_t len;
    if (!path || !path[0])
        return FL_RESULT_INVAL;
    strncpy(tmp, path, sizeof(tmp) - 1u);
    tmp[sizeof(tmp) - 1u] = '\0';
    len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] != '/')
            continue;
        tmp[i] = '\0';
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
            return FL_RESULT_ERR;
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
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
        "CREATE INDEX IF NOT EXISTS idx_catalog_transfer ON catalog_entry(transfer_id);"
        "CREATE INDEX IF NOT EXISTS idx_catalog_hash ON catalog_entry(content_hash);"
        "CREATE INDEX IF NOT EXISTS idx_catalog_receiver ON catalog_entry(receiver_member_id);";
    return db_exec(sql, schema);
}

static void fill_entry_from_stmt(sqlite3_stmt *st, fl_server_catalog_entry_t *out)
{
    const unsigned char *txt;
    memset(out, 0, sizeof(*out));
    txt = sqlite3_column_text(st, 0);
    if (txt)
        strncpy(out->content_hash, (const char *)txt, sizeof(out->content_hash) - 1u);
    txt = sqlite3_column_text(st, 1);
    if (txt)
        strncpy(out->transfer_id, (const char *)txt, sizeof(out->transfer_id) - 1u);
    out->payload_kind = (uint8_t)sqlite3_column_int(st, 2);
    out->sender_member_id = (uint16_t)sqlite3_column_int(st, 3);
    out->receiver_member_id = (uint16_t)sqlite3_column_int(st, 4);
    out->file_perms = (fl_file_perms_t)sqlite3_column_int(st, 5);
    out->is_public = (uint8_t)sqlite3_column_int(st, 6);
    out->created_at = (uint64_t)sqlite3_column_int64(st, 7);
    out->expires_at = (uint64_t)sqlite3_column_int64(st, 8);
    txt = sqlite3_column_text(st, 9);
    if (txt)
        strncpy(out->payload_name, (const char *)txt, sizeof(out->payload_name) - 1u);
    txt = sqlite3_column_text(st, 10);
    if (txt)
        strncpy(out->storage_relpath, (const char *)txt, sizeof(out->storage_relpath) - 1u);
    out->status = (fl_server_catalog_status_t)sqlite3_column_int(st, 11);
    out->total_bytes = (uint64_t)sqlite3_column_int64(st, 12);
}

static fl_result_t db_set_acl(sqlite3 *sql, const char *transfer_id,
                              uint16_t sender_id, uint16_t receiver_id, int is_public)
{
    sqlite3_stmt *st = NULL;
    const char *ins = "INSERT OR IGNORE INTO catalog_acl(transfer_id,member_id) VALUES(?,?);";
    if (!transfer_id || !transfer_id[0])
        return FL_RESULT_INVAL;
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, (int)sender_id);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    if (is_public || receiver_id == 0u)
        return FL_RESULT_OK;
    if (sqlite3_prepare_v2(sql, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, (int)receiver_id);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

static fl_result_t write_blob_file(const char *hash_hex, const uint8_t *data, size_t len,
                                   char *storage_relpath, size_t relpath_cap)
{
    char blobs_root[512];
    char blob_path[512];
    char rel[256];
    FILE *fp;

    if (catalog_blobs_root(blobs_root, sizeof(blobs_root)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (mkdir_p_simple(blobs_root) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    snprintf(rel, sizeof(rel), "%s/%s", FL_SERVER_CATALOG_BLOBS_DIR_NAME, hash_hex);
    if (join_path(blob_path, sizeof(blob_path), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    {
        char tmp[512];
        if (join_path(tmp, sizeof(tmp), blob_path, rel) != 0)
            return FL_RESULT_ERR;
        strncpy(blob_path, tmp, sizeof(blob_path) - 1u);
        blob_path[sizeof(blob_path) - 1u] = '\0';
    }
    fp = fopen(blob_path, "wb");
    if (!fp)
        return FL_RESULT_ERR;
    if (len > 0u && data && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        return FL_RESULT_ERR;
    }
    fclose(fp);
    if (snprintf(storage_relpath, relpath_cap, "%s", rel) < 0 ||
        strlen(storage_relpath) >= relpath_cap)
        return FL_RESULT_INVAL;
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_open(void)
{
    char db_path[512];
    if (s_catalog_db)
        return FL_RESULT_OK;
    if (catalog_db_path(db_path, sizeof(db_path)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (mkdir_p_simple(g_cwd) != FL_RESULT_OK) {
        char shared[512];
        if (join_path(shared, sizeof(shared), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
            return FL_RESULT_ERR;
        if (mkdir_p_simple(shared) != FL_RESULT_OK)
            return FL_RESULT_ERR;
    }
    {
        char shared[512];
        if (join_path(shared, sizeof(shared), g_cwd, FL_SERVER_SHARED_DIR_NAME) == 0)
            (void)mkdir_p_simple(shared);
    }
    if (sqlite3_open(db_path, &s_catalog_db) != SQLITE_OK) {
        s_catalog_db = NULL;
        return FL_RESULT_ERR;
    }
    if (db_init_schema(s_catalog_db) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_close(void)
{
    if (!s_catalog_db)
        return FL_RESULT_OK;
    sqlite3_close(s_catalog_db);
    s_catalog_db = NULL;
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_checkpoint(void)
{
    if (!s_catalog_db)
        (void)fl_server_catalog_open();
    if (!s_catalog_db)
        return FL_RESULT_ERR;
    return db_exec(s_catalog_db, "PRAGMA wal_checkpoint(FULL);");
}

fl_result_t fl_server_catalog_register_file_offer(const fl_server_file_offer_t *offer)
{
    char pending_hash[FL_SERVER_SHARE_ID_MAX + 16u];
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO catalog_entry(transfer_id,content_hash,payload_kind,"
        "sender_member_id,receiver_member_id,file_perms,is_public,created_at,"
        "expires_at,payload_name,storage_relpath,status,total_bytes) "
        "VALUES(?,?,?,?,?,?,?,?,?,?, '', ?, 0) "
        "ON CONFLICT(transfer_id) DO UPDATE SET "
        "sender_member_id=excluded.sender_member_id,"
        "receiver_member_id=excluded.receiver_member_id,"
        "file_perms=excluded.file_perms,"
        "is_public=excluded.is_public,"
        "expires_at=excluded.expires_at,"
        "payload_name=excluded.payload_name,"
        "status=excluded.status;";
    int is_public;
    uint64_t now;

    if (!offer || !offer->share_id[0])
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    snprintf(pending_hash, sizeof(pending_hash), "pending:%s", offer->share_id);
    is_public = (offer->receiver_member_id == 0u ||
                 (offer->file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    now = (uint64_t)time(NULL);
    if (sqlite3_prepare_v2(s_catalog_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, offer->share_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, pending_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_CHANNEL_PAYLOAD_FILE);
    sqlite3_bind_int(st, 4, (int)offer->sender_member_id);
    sqlite3_bind_int(st, 5, (int)offer->receiver_member_id);
    sqlite3_bind_int(st, 6, (int)offer->file_perms);
    sqlite3_bind_int(st, 7, is_public);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)now);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)offer->expires_at);
    sqlite3_bind_text(st, 10, offer->file_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 11, (int)FL_SERVER_CATALOG_PENDING);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_commit_file_offer(const fl_server_file_offer_t *offer,
                                                const uint8_t *data,
                                                size_t data_len)
{
    char hash_hex[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char storage_relpath[FL_SERVER_SHARE_REL_PATH_MAX];
    sqlite3_stmt *st = NULL;
    const char *upd =
        "UPDATE catalog_entry SET content_hash=?, storage_relpath=?, "
        "status=?, total_bytes=? WHERE transfer_id=?;";
    int is_public;

    if (!offer || !offer->share_id[0])
        return FL_RESULT_INVAL;
    if (data_len > FL_SERVER_CATALOG_MAX_BYTES)
        return FL_RESULT_NOMEM;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_server_shared_sha256_hex(data, data_len, hash_hex, sizeof(hash_hex)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (write_blob_file(hash_hex, data, data_len, storage_relpath,
                        sizeof(storage_relpath)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, upd, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, storage_relpath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)data_len);
    sqlite3_bind_text(st, 5, offer->share_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    is_public = (offer->receiver_member_id == 0u ||
                 (offer->file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    return db_set_acl(s_catalog_db, offer->share_id, offer->sender_member_id,
                      offer->receiver_member_id, is_public);
}

fl_result_t fl_server_catalog_revoke_transfer(const char *transfer_id)
{
    sqlite3_stmt *st = NULL;
    const char *upd = "UPDATE catalog_entry SET status=? WHERE transfer_id=?;";
    if (!transfer_id || !transfer_id[0])
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, upd, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int(st, 1, (int)FL_SERVER_CATALOG_REVOKED);
    sqlite3_bind_text(st, 2, transfer_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_register_message_pending(const char *transfer_id,
                                                       uint16_t sender_member_id,
                                                       uint16_t receiver_member_id,
                                                       uint64_t expires_at,
                                                       fl_file_perms_t file_perms,
                                                       const char *payload_name)
{
    char pending_hash[FL_SERVER_SHARE_ID_MAX + 16u];
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO catalog_entry(transfer_id,content_hash,payload_kind,"
        "sender_member_id,receiver_member_id,file_perms,is_public,created_at,"
        "expires_at,payload_name,storage_relpath,status,total_bytes) "
        "VALUES(?,?,?,?,?,?,?,?,?,?, '', ?, 0) "
        "ON CONFLICT(transfer_id) DO UPDATE SET "
        "sender_member_id=excluded.sender_member_id,"
        "receiver_member_id=excluded.receiver_member_id,"
        "file_perms=excluded.file_perms,"
        "is_public=excluded.is_public,"
        "expires_at=excluded.expires_at,"
        "payload_name=excluded.payload_name,"
        "status=excluded.status;";
    int is_public;
    uint64_t now;
    const char *name = payload_name ? payload_name : "message";

    if (!transfer_id || !transfer_id[0])
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    snprintf(pending_hash, sizeof(pending_hash), "pending:%s", transfer_id);
    is_public = (receiver_member_id == 0u ||
                 (file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    now = (uint64_t)time(NULL);
    if (sqlite3_prepare_v2(s_catalog_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, pending_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_CHANNEL_PAYLOAD_MSG);
    sqlite3_bind_int(st, 4, (int)sender_member_id);
    sqlite3_bind_int(st, 5, (int)receiver_member_id);
    sqlite3_bind_int(st, 6, (int)file_perms);
    sqlite3_bind_int(st, 7, is_public);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)now);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)expires_at);
    sqlite3_bind_text(st, 10, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 11, (int)FL_SERVER_CATALOG_PENDING);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return db_set_acl(s_catalog_db, transfer_id, sender_member_id, receiver_member_id,
                      is_public);
}

fl_result_t fl_server_catalog_commit_message(const char *transfer_id,
                                             const uint8_t *body,
                                             size_t body_len)
{
    char hash_hex[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char storage_relpath[FL_SERVER_SHARE_REL_PATH_MAX];
    sqlite3_stmt *st = NULL;
    const char *upd =
        "UPDATE catalog_entry SET content_hash=?, storage_relpath=?, "
        "status=?, total_bytes=? WHERE transfer_id=? AND payload_kind=?;";
    fl_server_catalog_entry_t entry;

    if (!transfer_id || !transfer_id[0])
        return FL_RESULT_INVAL;
    if (!body && body_len > 0u)
        return FL_RESULT_INVAL;
    if (body_len > FL_SERVER_CATALOG_MAX_BYTES)
        return FL_RESULT_NOMEM;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_server_catalog_lookup_transfer(transfer_id, &entry) != FL_RESULT_OK)
        return FL_RESULT_NOENT;
    if (entry.payload_kind != FL_CHANNEL_PAYLOAD_MSG)
        return FL_RESULT_INVAL;
    if (fl_server_shared_sha256_hex(body, body_len, hash_hex, sizeof(hash_hex)) !=
        FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (write_blob_file(hash_hex, body, body_len, storage_relpath,
                        sizeof(storage_relpath)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, upd, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, storage_relpath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)body_len);
    sqlite3_bind_text(st, 5, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, (int)FL_CHANNEL_PAYLOAD_MSG);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_store_message(uint16_t sender_member_id,
                                            uint16_t receiver_member_id,
                                            const char *payload_name,
                                            const uint8_t *body,
                                            size_t body_len,
                                            uint64_t expires_at,
                                            fl_file_perms_t file_perms,
                                            char *out_transfer_id,
                                            size_t out_transfer_id_cap,
                                            char *out_content_hash,
                                            size_t out_hash_cap)
{
    char hash_hex[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char storage_relpath[FL_SERVER_SHARE_REL_PATH_MAX];
    char transfer_id[FL_SERVER_SHARE_ID_MAX];
    sqlite3_stmt *st = NULL;
    const char *ins =
        "INSERT INTO catalog_entry(transfer_id,content_hash,payload_kind,"
        "sender_member_id,receiver_member_id,file_perms,is_public,created_at,"
        "expires_at,payload_name,storage_relpath,status,total_bytes) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);";
    int is_public;
    uint64_t now = (uint64_t)time(NULL);
    const char *name = payload_name ? payload_name : "message";

    if (!body && body_len > 0u)
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_server_shared_sha256_hex(body, body_len, hash_hex, sizeof(hash_hex)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    s_msg_serial++;
    snprintf(transfer_id, sizeof(transfer_id), "msg-%lu-%u",
             (unsigned long)now, s_msg_serial);
    if (write_blob_file(hash_hex, body, body_len, storage_relpath,
                        sizeof(storage_relpath)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    is_public = (receiver_member_id == 0u ||
                 (file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    if (sqlite3_prepare_v2(s_catalog_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_CHANNEL_PAYLOAD_MSG);
    sqlite3_bind_int(st, 4, (int)sender_member_id);
    sqlite3_bind_int(st, 5, (int)receiver_member_id);
    sqlite3_bind_int(st, 6, (int)file_perms);
    sqlite3_bind_int(st, 7, is_public);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)now);
    sqlite3_bind_int64(st, 9, (sqlite3_int64)expires_at);
    sqlite3_bind_text(st, 10, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, storage_relpath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 12, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 13, (sqlite3_int64)body_len);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    if (db_set_acl(s_catalog_db, transfer_id, sender_member_id, receiver_member_id,
                   is_public) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (out_transfer_id && out_transfer_id_cap > 0u) {
        strncpy(out_transfer_id, transfer_id, out_transfer_id_cap - 1u);
        out_transfer_id[out_transfer_id_cap - 1u] = '\0';
    }
    if (out_content_hash && out_hash_cap > 0u) {
        strncpy(out_content_hash, hash_hex, out_hash_cap - 1u);
        out_content_hash[out_hash_cap - 1u] = '\0';
    }
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_member_can_fetch(const fl_server_catalog_entry_t *entry,
                                               uint16_t member_id,
                                               uint64_t now)
{
    sqlite3_stmt *st = NULL;
    if (!entry)
        return FL_RESULT_INVAL;
    if (entry->status != FL_SERVER_CATALOG_COMPLETE)
        return FL_RESULT_ACCES;
    if (entry->expires_at != 0u && now > entry->expires_at)
        return FL_RESULT_ACCES;
    if (entry->is_public || entry->receiver_member_id == 0u)
        return FL_RESULT_OK;
    if (member_id == entry->sender_member_id || member_id == entry->receiver_member_id)
        return FL_RESULT_OK;
    if (!s_catalog_db && fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db,
                           "SELECT 1 FROM catalog_acl WHERE transfer_id=? AND member_id=? LIMIT 1;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, entry->transfer_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, (int)member_id);
    if (sqlite3_step(st) == SQLITE_ROW) {
        sqlite3_finalize(st);
        return FL_RESULT_OK;
    }
    sqlite3_finalize(st);
    return FL_RESULT_ACCES;
}

fl_result_t fl_server_catalog_lookup_transfer(const char *transfer_id,
                                              fl_server_catalog_entry_t *out)
{
    sqlite3_stmt *st = NULL;
    const char *q =
        "SELECT content_hash,transfer_id,payload_kind,sender_member_id,"
        "receiver_member_id,file_perms,is_public,created_at,expires_at,"
        "payload_name,storage_relpath,status,total_bytes "
        "FROM catalog_entry WHERE transfer_id=? LIMIT 1;";
    if (!transfer_id || !out)
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        return FL_RESULT_NOENT;
    }
    fill_entry_from_stmt(st, out);
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_read_blob(const fl_server_catalog_entry_t *entry,
                                        uint8_t *out_data,
                                        size_t out_data_cap,
                                        size_t *out_data_len)
{
    char path[512];
    FILE *fp;
    size_t n;

    if (!entry || !out_data_len)
        return FL_RESULT_INVAL;
    *out_data_len = 0u;
    if (entry->status != FL_SERVER_CATALOG_COMPLETE)
        return FL_RESULT_ACCES;
    if (entry->storage_relpath[0] == '\0')
        return FL_RESULT_NOENT;
    if (entry->total_bytes > FL_SERVER_CATALOG_MAX_BYTES)
        return FL_RESULT_NOMEM;
    if (entry->total_bytes > 0u &&
        (!out_data || out_data_cap < (size_t)entry->total_bytes))
        return FL_RESULT_NOMEM;
    if (join_path(path, sizeof(path), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    {
        char tmp[512];
        if (join_path(tmp, sizeof(tmp), path, entry->storage_relpath) != 0)
            return FL_RESULT_ERR;
        strncpy(path, tmp, sizeof(path) - 1u);
        path[sizeof(path) - 1u] = '\0';
    }
    fp = fopen(path, "rb");
    if (!fp)
        return FL_RESULT_ERR;
    n = fread(out_data, 1, (size_t)entry->total_bytes, fp);
    if (ferror(fp)) {
        fclose(fp);
        return FL_RESULT_ERR;
    }
    fclose(fp);
    if (n != (size_t)entry->total_bytes) {
        *out_data_len = n;
        return FL_RESULT_ERR;
    }
    *out_data_len = n;
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_fetch_by_hash(const char *content_hash_hex,
                                            uint16_t member_id,
                                            uint64_t now,
                                            fl_server_catalog_entry_t *out_meta,
                                            uint8_t *out_data,
                                            size_t out_data_cap,
                                            size_t *out_data_len)
{
    sqlite3_stmt *st = NULL;
    fl_server_catalog_entry_t row;
    fl_result_t rc = FL_RESULT_NOENT;
    const char *q =
        "SELECT content_hash,transfer_id,payload_kind,sender_member_id,"
        "receiver_member_id,file_perms,is_public,created_at,expires_at,"
        "payload_name,storage_relpath,status,total_bytes "
        "FROM catalog_entry WHERE content_hash=? AND status=? "
        "ORDER BY created_at DESC;";

    if (!content_hash_hex || !out_meta)
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, content_hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, (int)FL_SERVER_CATALOG_COMPLETE);
    while (sqlite3_step(st) == SQLITE_ROW) {
        fill_entry_from_stmt(st, &row);
        rc = fl_server_catalog_member_can_fetch(&row, member_id, now);
        if (rc == FL_RESULT_OK)
            break;
        if (rc != FL_RESULT_ACCES)
            break;
    }
    sqlite3_finalize(st);
    if (rc != FL_RESULT_OK)
        return rc;
    *out_meta = row;
    if (!out_data || out_data_cap == 0u || !out_data_len)
        return FL_RESULT_OK;
    return fl_server_catalog_read_blob(&row, out_data, out_data_cap, out_data_len);
}

fl_result_t fl_server_catalog_list_for_member(uint16_t member_id,
                                            uint64_t now,
                                            fl_server_catalog_entry_t *out,
                                            size_t out_cap,
                                            size_t *out_count)
{
    sqlite3_stmt *st = NULL;
    const char *q =
        "SELECT content_hash,transfer_id,payload_kind,sender_member_id,"
        "receiver_member_id,file_perms,is_public,created_at,expires_at,"
        "payload_name,storage_relpath,status,total_bytes "
        "FROM catalog_entry WHERE status=? ORDER BY created_at DESC;";
    size_t n = 0;

    if (!out || !out_count || out_cap == 0u)
        return FL_RESULT_INVAL;
    *out_count = 0;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, q, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int(st, 1, (int)FL_SERVER_CATALOG_COMPLETE);
    while (sqlite3_step(st) == SQLITE_ROW && n < out_cap) {
        fl_server_catalog_entry_t row;
        fill_entry_from_stmt(st, &row);
        if (fl_server_catalog_member_can_fetch(&row, member_id, now) == FL_RESULT_OK)
            out[n++] = row;
    }
    sqlite3_finalize(st);
    *out_count = n;
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_purge_expired(uint64_t now)
{
    sqlite3_stmt *st = NULL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db,
                           "UPDATE catalog_entry SET status=? "
                           "WHERE status=? AND expires_at!=0 AND expires_at<?;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int(st, 1, (int)FL_SERVER_CATALOG_EXPIRED);
    sqlite3_bind_int(st, 2, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)now);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_import_handoff(const char *src_server_shared_dir)
{
    char src_db[512];
    char dst_db[512];
    char src_blobs[512];
    char dst_blobs[512];
    FILE *in;
    FILE *out;
    char buf[4096];
    size_t n;

    if (!src_server_shared_dir || !src_server_shared_dir[0])
        return FL_RESULT_INVAL;
    if (join_path(src_db, sizeof(src_db), src_server_shared_dir, FL_SERVER_CATALOG_DB_NAME) != 0)
        return FL_RESULT_ERR;
    if (catalog_db_path(dst_db, sizeof(dst_db)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (join_path(src_blobs, sizeof(src_blobs), src_server_shared_dir,
                  FL_SERVER_CATALOG_BLOBS_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (catalog_blobs_root(dst_blobs, sizeof(dst_blobs)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    (void)fl_server_catalog_close();
    in = fopen(src_db, "rb");
    if (!in)
        return FL_RESULT_ERR;
    out = fopen(dst_db, "wb");
    if (!out) {
        fclose(in);
        return FL_RESULT_ERR;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0u) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return FL_RESULT_ERR;
        }
    }
    fclose(in);
    fclose(out);
    (void)mkdir_p_simple(dst_blobs);
    /* Best-effort: copy known blob files listed in source DB after reopen. */
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    {
        sqlite3 *src_sql = NULL;
        sqlite3_stmt *st = NULL;
        if (sqlite3_open_v2(src_db, &src_sql, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK &&
            sqlite3_prepare_v2(src_sql,
                               "SELECT storage_relpath FROM catalog_entry WHERE storage_relpath!='';",
                               -1, &st, NULL) == SQLITE_OK) {
            while (sqlite3_step(st) == SQLITE_ROW) {
                const char *rel = (const char *)sqlite3_column_text(st, 0);
                char from[512];
                char to[512];
                if (!rel)
                    continue;
                if (join_path(from, sizeof(from), src_server_shared_dir, rel) != 0)
                    continue;
                if (join_path(to, sizeof(to), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
                    continue;
                {
                    char tmp[512];
                    if (join_path(tmp, sizeof(tmp), to, rel) != 0)
                        continue;
                    strncpy(to, tmp, sizeof(to) - 1u);
                    to[sizeof(to) - 1u] = '\0';
                }
                in = fopen(from, "rb");
                if (!in)
                    continue;
                out = fopen(to, "wb");
                if (!out) {
                    fclose(in);
                    continue;
                }
                while ((n = fread(buf, 1, sizeof(buf), in)) > 0u)
                    (void)fwrite(buf, 1, n, out);
                fclose(in);
                fclose(out);
            }
            sqlite3_finalize(st);
        }
        if (src_sql)
            sqlite3_close(src_sql);
    }
    return FL_RESULT_OK;
}
