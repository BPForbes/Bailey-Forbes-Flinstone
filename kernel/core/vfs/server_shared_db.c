#include "server_shared_db.h"
#include "server_shared_digest.h"
#include "server_shared_fs.h"

#include "common.h"
#include "fl/path.h"
#include "fl/sqlite_schema.h"
#include "mem_domain.h"
#include "contract_p5_server_share.h"

#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static sqlite3 *s_catalog_db;

static fl_result_t catalog_db_path(char *out, size_t cap)
{
    char root[512];
    if (fl_fs_join_path(root, sizeof(root), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(out, cap, root, FL_SERVER_CATALOG_DB_NAME) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

static fl_result_t catalog_blobs_root(char *out, size_t cap)
{
    char root[512];
    if (fl_fs_join_path(root, sizeof(root), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    if (fl_fs_join_path(out, cap, root, FL_SERVER_CATALOG_BLOBS_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    return FL_RESULT_OK;
}

/* Missing directories are created through the share's one mkdir -p (server_shared_fs.c). */
static fl_result_t mkdir_p_simple(const char *path)
{
    return fl_fs_mkdir_p(path, 0700) == 0 ? FL_RESULT_OK : FL_RESULT_ERR;
}

/* The ordinary share path serves and revokes only these kinds; the reserved KQ kind (and any
 * kind not listed) is refused, so a row of another family is never reachable through it. */
static int payload_kind_is_shareable(uint8_t kind)
{
    return kind == FL_CHANNEL_PAYLOAD_FILE || kind == FL_CHANNEL_PAYLOAD_MSG;
}

#define SHAREABLE_KINDS_SQL FL_CATALOG_SHAREABLE_KINDS_SQL_C

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
    {
        /* A stored value outside 0..255 is not truncated into a valid kind: 0 is refused. */
        sqlite3_int64 k = sqlite3_column_int64(st, 2);
        out->payload_kind = (k >= 0 && k <= 255) ? (uint8_t)k : 0u;
    }
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
    {
        int rel_len = snprintf(rel, sizeof(rel), "%s/%s", FL_SERVER_CATALOG_BLOBS_DIR_NAME,
                               hash_hex);
        if (rel_len < 0 || (size_t)rel_len >= sizeof(rel))
            return FL_RESULT_ERR;
    }
    if (fl_fs_join_path(blob_path, sizeof(blob_path), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    {
        char tmp[512];
        if (fl_fs_join_path(tmp, sizeof(tmp), blob_path, rel) != 0)
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
        if (fl_fs_join_path(shared, sizeof(shared), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
            return FL_RESULT_ERR;
        if (mkdir_p_simple(shared) != FL_RESULT_OK)
            return FL_RESULT_ERR;
    }
    {
        char shared[512];
        if (fl_fs_join_path(shared, sizeof(shared), g_cwd, FL_SERVER_SHARED_DIR_NAME) == 0)
            (void)mkdir_p_simple(shared);
    }
    return fl_catalog_file_open(db_path, &s_catalog_db);
}

fl_result_t fl_server_catalog_close(void)
{
    if (!s_catalog_db)
        return FL_RESULT_OK;
    sqlite3_close(s_catalog_db);
    s_catalog_db = NULL;
    return FL_RESULT_OK;
}

/* One unit of work: a state change and its event commit together or not at all. */
fl_result_t fl_server_catalog_begin(void)
{
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return fl_sqlite_exec(s_catalog_db, "server_catalog", "BEGIN IMMEDIATE;");
}

fl_result_t fl_server_catalog_commit(void)
{
    return fl_sqlite_exec(s_catalog_db, "server_catalog", "COMMIT;");
}

void fl_server_catalog_rollback(void)
{
    (void)fl_sqlite_exec(s_catalog_db, "server_catalog", "ROLLBACK;");
}

fl_result_t fl_server_catalog_checkpoint(void)
{
    if (!s_catalog_db)
        (void)fl_server_catalog_open();
    if (!s_catalog_db)
        return FL_RESULT_ERR;
    return fl_sqlite_exec(s_catalog_db, "server_catalog", "PRAGMA wal_checkpoint(FULL);");
}

fl_result_t fl_server_catalog_append_event(const char *transfer_id, const char *event,
                                           uint16_t actor_member_id, uint64_t at,
                                           const char *detail)
{
    if (!transfer_id || !transfer_id[0] || !event || !event[0])
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return fl_catalog_file_append_event(s_catalog_db, 0, transfer_id, event, actor_member_id, at,
                                        detail);
}

fl_result_t fl_server_catalog_list_events(const char *transfer_id,
                                          fl_server_catalog_event_t *out, size_t out_cap,
                                          size_t *out_count)
{
    if (out_count)
        *out_count = 0;
    if (!transfer_id || !transfer_id[0] || !out || !out_count || out_cap == 0u)
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    return fl_catalog_file_list_events(s_catalog_db, 0, transfer_id, out, out_cap, out_count);
}

static fl_result_t catalog_lookup_any(const char *transfer_id, fl_server_catalog_entry_t *out);

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
        "status=excluded.status "
        "WHERE catalog_entry.payload_kind=excluded.payload_kind;";
    int is_public;
    uint64_t now;

    if (!offer || !offer->share_id[0])
        return FL_RESULT_INVAL;
    if (offer->file_name[0] && !fl_fs_name_is_plain(offer->file_name))
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    snprintf(pending_hash, sizeof(pending_hash), "pending:%s", offer->share_id);
    is_public = (offer->receiver_member_id == 0u ||
                 (offer->file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    now = (uint64_t)time(NULL);
    if (sqlite3_prepare_v2(s_catalog_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    if (fl_server_catalog_begin() != FL_RESULT_OK) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
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
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    if (sqlite3_changes(s_catalog_db) == 0) {
        /* The id belongs to a row of another kind: nothing is changed, recorded or granted. */
        fl_server_catalog_rollback();
        return FL_RESULT_ACCES;
    }
    if (fl_server_catalog_append_event(offer->share_id, "offered", offer->sender_member_id,
                                       now, offer->file_name) != FL_RESULT_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    if (fl_server_catalog_commit() != FL_RESULT_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
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
        "status=?, total_bytes=? WHERE transfer_id=? AND payload_kind=?;";
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
    if (fl_server_catalog_begin() != FL_RESULT_OK) {
        sqlite3_finalize(st);
        return FL_RESULT_ERR;
    }
    sqlite3_bind_text(st, 1, hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, storage_relpath, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 4, (sqlite3_int64)data_len);
    sqlite3_bind_text(st, 5, offer->share_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, (int)FL_CHANNEL_PAYLOAD_FILE);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    if (sqlite3_changes(s_catalog_db) == 0) {
        fl_server_catalog_entry_t other;
        /* A row of another kind is refused untouched; an id with no row gets no history or
         * grants either (they would otherwise be inherited by a later row of that id). */
        fl_server_catalog_rollback();
        return catalog_lookup_any(offer->share_id, &other) == FL_RESULT_OK ? FL_RESULT_ACCES
                                                                           : FL_RESULT_NOENT;
    }
    if (fl_server_catalog_append_event(offer->share_id, "completed", offer->sender_member_id,
                                       (uint64_t)time(NULL), hash_hex) != FL_RESULT_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    is_public = (offer->receiver_member_id == 0u ||
                 (offer->file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    if (db_set_acl(s_catalog_db, offer->share_id, offer->sender_member_id,
                   offer->receiver_member_id, is_public) != FL_RESULT_OK ||
        fl_server_catalog_commit() != FL_RESULT_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_server_catalog_revoke_transfer(const char *transfer_id)
{
    sqlite3_stmt *st = NULL;
    const char *upd =
        "UPDATE catalog_entry SET status=? WHERE transfer_id=? AND " SHAREABLE_KINDS_SQL ";";
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
    if (sqlite3_changes(s_catalog_db) == 0) {
        /* No row changed. A missing id and a reserved id both answer
         * FL_RESULT_NOENT, and neither row is modified. A shareable row that
         * was already revoked is unchanged and still answers FL_RESULT_OK. */
        fl_server_catalog_entry_t other;
        fl_result_t looked = catalog_lookup_any(transfer_id, &other);
        if (looked == FL_RESULT_NOENT ||
            (looked == FL_RESULT_OK && !payload_kind_is_shareable(other.payload_kind)))
            return FL_RESULT_NOENT;
        if (looked != FL_RESULT_OK)
            return looked;
    }
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
        "status=excluded.status "
        "WHERE catalog_entry.payload_kind=excluded.payload_kind;";
    int is_public;
    uint64_t now;
    const char *name = payload_name ? payload_name : "message";

    if (!transfer_id || !transfer_id[0])
        return FL_RESULT_INVAL;
    if (payload_name && payload_name[0] && !fl_fs_name_is_plain(payload_name))
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
    if (sqlite3_changes(s_catalog_db) == 0)
        return FL_RESULT_ACCES; /* the id belongs to a row of another kind */
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
    if (payload_name && payload_name[0] && !fl_fs_name_is_plain(payload_name))
        return FL_RESULT_INVAL;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (fl_server_shared_sha256_hex(body, body_len, hash_hex, sizeof(hash_hex)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (write_blob_file(hash_hex, body, body_len, storage_relpath,
                        sizeof(storage_relpath)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    is_public = (receiver_member_id == 0u ||
                 (file_perms & FL_FILE_FLAG_PUBLIC) != 0) ? 1 : 0;
    if (sqlite3_prepare_v2(s_catalog_db, ins, -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    {
        int attempt;
        int inserted = 0;

        for (attempt = 0; attempt < 3; attempt++) {
            int rc;
            int ex;

            if (fl_server_shared_uuid_v4_lower(transfer_id, sizeof(transfer_id)) !=
                FL_RESULT_OK) {
                sqlite3_finalize(st);
                return FL_RESULT_ERR;
            }
            if (attempt > 0) {
                sqlite3_reset(st);
                sqlite3_clear_bindings(st);
            }
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
            rc = sqlite3_step(st);
            if (rc == SQLITE_DONE) {
                inserted = 1;
                break;
            }
            ex = sqlite3_extended_errcode(s_catalog_db);
            if (ex == SQLITE_CONSTRAINT_PRIMARYKEY || ex == SQLITE_CONSTRAINT)
                continue;
            sqlite3_finalize(st);
            return FL_RESULT_ERR;
        }
        sqlite3_finalize(st);
        if (!inserted)
            return FL_RESULT_ERR;
    }
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
    if (!payload_kind_is_shareable(entry->payload_kind))
        return FL_RESULT_ACCES;
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

static fl_result_t catalog_lookup_any(const char *transfer_id, fl_server_catalog_entry_t *out)
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

fl_result_t fl_server_catalog_lookup_transfer(const char *transfer_id,
                                              fl_server_catalog_entry_t *out)
{
    fl_result_t rc = catalog_lookup_any(transfer_id, out);
    if (rc == FL_RESULT_OK && !payload_kind_is_shareable(out->payload_kind)) {
        /* Same answer as an id that does not exist, so the id's presence is not revealed. */
        memset(out, 0, sizeof(*out));
        return FL_RESULT_NOENT;
    }
    return rc;
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
    if (!payload_kind_is_shareable(entry->payload_kind))
        return FL_RESULT_ACCES;
    if (entry->status != FL_SERVER_CATALOG_COMPLETE)
        return FL_RESULT_ACCES;
    if (entry->storage_relpath[0] == '\0')
        return FL_RESULT_NOENT;
    if (entry->total_bytes > FL_SERVER_CATALOG_MAX_BYTES)
        return FL_RESULT_NOMEM;
    if (entry->total_bytes > 0u &&
        (!out_data || out_data_cap < (size_t)entry->total_bytes))
        return FL_RESULT_NOMEM;
    if (fl_fs_join_path(path, sizeof(path), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0)
        return FL_RESULT_ERR;
    {
        char tmp[512];
        if (fl_fs_join_path(tmp, sizeof(tmp), path, entry->storage_relpath) != 0)
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
        "FROM catalog_entry WHERE content_hash=? AND status=? AND " SHAREABLE_KINDS_SQL " "
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
        "FROM catalog_entry WHERE status=? AND " SHAREABLE_KINDS_SQL " ORDER BY created_at DESC;";
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
    /* One statement records every share about to expire, before its status changes. */
    const char *log_expired =
        "INSERT INTO catalog_event(transfer_id,seq,event,actor_member_id,at,detail) "
        "SELECT c.transfer_id, (SELECT COALESCE(MAX(e.seq),0)+1 FROM catalog_event e "
        "WHERE e.transfer_id=c.transfer_id), 'expired', 0, ?, '' "
        "FROM catalog_entry c WHERE c.status=? AND c.expires_at!=0 AND c.expires_at<? AND "
        "COALESCE(c.payload_kind,0) IN (1,2);";
    const char *expire =
        "UPDATE catalog_entry SET status=? "
        "WHERE status=? AND expires_at!=0 AND expires_at<? AND " SHAREABLE_KINDS_SQL ";";
    if (fl_server_catalog_begin() != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (sqlite3_prepare_v2(s_catalog_db, log_expired, -1, &st, NULL) != SQLITE_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int64(st, 1, (sqlite3_int64)now);
    sqlite3_bind_int(st, 2, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)now);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    st = NULL;
    if (sqlite3_prepare_v2(s_catalog_db, expire, -1, &st, NULL) != SQLITE_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_bind_int(st, 1, (int)FL_SERVER_CATALOG_EXPIRED);
    sqlite3_bind_int(st, 2, (int)FL_SERVER_CATALOG_COMPLETE);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)now);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    if (fl_server_catalog_commit() != FL_RESULT_OK) {
        fl_server_catalog_rollback();
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

/* Reads a catalog file without touching the live handle: its schema version and how many rows
 * carry a kind the share path does not serve (the reserved KQ kind). Fails closed: a locked,
 * corrupt or non-database file, or any statement that does not finish, is an error. Only a
 * missing file (when allowed) or a database without the table counts as empty. */
static fl_result_t catalog_file_probe(const char *path, int may_be_missing, int *version,
                                      int *reserved_rows)
{
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    int has_table = 0;
    int step;
    fl_result_t rc = FL_RESULT_ERR;

    *version = 0;
    *reserved_rows = 0;
    if (access(path, F_OK) != 0)
        return (may_be_missing && errno == ENOENT) ? FL_RESULT_OK : FL_RESULT_ERR;
    if (sqlite3_open_v2(path, &sql, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto done;
    sqlite3_busy_timeout(sql, FL_SERVER_CATALOG_PROBE_TIMEOUT_MS);
    if (fl_catalog_file_user_version(sql, version) != FL_RESULT_OK || *version < 0)
        goto done;
    if (sqlite3_prepare_v2(sql,
                           "SELECT 1 FROM sqlite_master WHERE type='table' AND "
                           "name='catalog_entry';", -1, &st, NULL) != SQLITE_OK)
        goto done;
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    st = NULL;
    if (step == SQLITE_ROW)
        has_table = 1;
    else if (step != SQLITE_DONE)
        goto done;
    if (has_table) {
        if (sqlite3_prepare_v2(sql,
                               "SELECT COUNT(*) FROM catalog_entry WHERE NOT "
                               SHAREABLE_KINDS_SQL ";", -1, &st, NULL) != SQLITE_OK)
            goto done;
        if (sqlite3_step(st) != SQLITE_ROW)
            goto done;
        *reserved_rows = sqlite3_column_int(st, 0);
    }
    rc = FL_RESULT_OK;
done:
    if (st)
        sqlite3_finalize(st);
    if (sql)
        sqlite3_close(sql);
    return rc;
}

static fl_result_t copy_fd_to_fd(int in, int out, unsigned long long limit)
{
    uint8_t buf[4096];
    unsigned long long total = 0;
    for (;;) {
        ssize_t n = read(in, buf, sizeof(buf));
        size_t off = 0;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return FL_RESULT_ERR;
        }
        if (n == 0)
            return FL_RESULT_OK;
        total += (unsigned long long)n;
        if (total > limit)
            return FL_RESULT_NOMEM;
        while (off < (size_t)n) {
            ssize_t w = write(out, buf + off, (size_t)n - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return FL_RESULT_ERR;
            }
            off += (size_t)w;
        }
    }
}

/* Copies the source file to a new owner-only temp file with a name no other import shares
 * (mkstemp: created new, mode 0600, never through a link). tmp_path holds the template on entry
 * and the real name on success. The caller probes and uses THAT file, so what was checked is
 * what is installed. A source over FL_SERVER_CATALOG_MAX_DB_BYTES is refused. */
static fl_result_t copy_source_to_temp(const char *src_db, char *tmp_path)
{
    struct stat sb;
    fl_result_t rc;
    int out;
    int in = open(src_db, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0)
        return FL_RESULT_ERR;
    if (fstat(in, &sb) != 0 || !S_ISREG(sb.st_mode) ||
        (unsigned long long)sb.st_size > FL_SERVER_CATALOG_MAX_DB_BYTES) {
        close(in);
        return FL_RESULT_ERR;
    }
    out = mkstemp(tmp_path);
    if (out < 0) {
        close(in);
        return FL_RESULT_ERR;
    }
    rc = copy_fd_to_fd(in, out, FL_SERVER_CATALOG_MAX_DB_BYTES);
    close(in);
    if (rc == FL_RESULT_OK && (fchmod(out, FL_SERVER_CATALOG_FILE_MODE) != 0 || fsync(out) != 0))
        rc = FL_RESULT_ERR;
    if (close(out) != 0)
        rc = FL_RESULT_ERR;
    if (rc != FL_RESULT_OK)
        (void)unlink(tmp_path);
    return rc == FL_RESULT_NOMEM ? FL_RESULT_ERR : rc;
}

/* True when SQLite left a journal, WAL or shared-memory file beside this database: a write is
 * in flight, was cut short, or lives outside the main file, so a plain file copy (or a rename
 * over it) could lose or tear data. */
static int sidecar_present(const char *db_path)
{
    static const char *const suffix[] = {"-journal", "-wal", "-shm"};
    char p[600];
    size_t i;
    for (i = 0; i < sizeof(suffix) / sizeof(suffix[0]); i++) {
        if (snprintf(p, sizeof(p), "%s%s", db_path, suffix[i]) >= (int)sizeof(p))
            return 1;
        if (access(p, F_OK) == 0 || errno != ENOENT)
            return 1;
    }
    return 0;
}

/* A stored blob path is accepted only as "blobs/<one plain name>". */
static int blob_relpath_name(const char *rel, const char **name)
{
    size_t pre = strlen(FL_SERVER_CATALOG_BLOBS_DIR_NAME);
    if (!rel || strncmp(rel, FL_SERVER_CATALOG_BLOBS_DIR_NAME, pre) != 0 || rel[pre] != '/')
        return 0;
    *name = rel + pre + 1u;
    return fl_fs_name_is_plain(*name);
}

/* Copies every blob the (probed) catalog file names, through the contained helpers. Any bad
 * path, missing blob or failed write is an error: nothing is skipped. A blobs directory that
 * is a link fails closed even when no row has a storage_relpath. */
static fl_result_t copy_blobs_listed_in(const char *db_file, const char *src_shared_dir,
                                        const char *dst_blobs)
{
    sqlite3 *sql = NULL;
    sqlite3_stmt *st = NULL;
    char src_blobs[512];
    int src_fd = -1;
    int dst_fd = -1;
    fl_result_t rc = FL_RESULT_ERR;
    int step;

    if (sqlite3_open_v2(db_file, &sql, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
        goto done;
    sqlite3_busy_timeout(sql, FL_SERVER_CATALOG_PROBE_TIMEOUT_MS);
    /* A file without the table (an empty catalog) names no blobs. */
    if (sqlite3_prepare_v2(sql, "SELECT 1 FROM sqlite_master WHERE type='table' AND "
                                "name='catalog_entry';", -1, &st, NULL) != SQLITE_OK)
        goto done;
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    st = NULL;
    if (step != SQLITE_DONE && step != SQLITE_ROW)
        goto done;
    if (fl_fs_join_path(src_blobs, sizeof(src_blobs), src_shared_dir,
                        FL_SERVER_CATALOG_BLOBS_DIR_NAME) != 0)
        goto done;
    if (step == SQLITE_ROW) {
        if (sqlite3_prepare_v2(sql, "SELECT storage_relpath,content_hash FROM catalog_entry "
                                    "WHERE storage_relpath!='';",
                               -1, &st, NULL) != SQLITE_OK)
            goto done;
        if (mkdir_p_simple(dst_blobs) != FL_RESULT_OK)
            goto done;
    }
    /* Before any success return, including a catalog that names no blob. Open
     * does not follow a final-component link (O_NOFOLLOW) and a failed open is
     * not used as a path to write through. A directory that is not there is
     * not a link; anything else we cannot open this way fails closed. */
    dst_fd = fl_fs_dir_open_nofollow(dst_blobs);
    if (dst_fd < 0 && errno != ENOENT)
        goto done;
    src_fd = fl_fs_dir_open_nofollow(src_blobs);
    if (src_fd < 0 && errno != ENOENT)
        goto done;
    if (step == SQLITE_DONE) {
        rc = FL_RESULT_OK;
        goto done;
    }
    rc = FL_RESULT_OK;
    while (rc == FL_RESULT_OK && (step = sqlite3_step(st)) == SQLITE_ROW) {
        const char *name;
        const char *want_hash;
        char got_hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
        uint8_t *data = NULL;
        size_t len = 0;
        if (!blob_relpath_name((const char *)sqlite3_column_text(st, 0), &name)) {
            rc = FL_RESULT_INVAL;
            break;
        }
        want_hash = (const char *)sqlite3_column_text(st, 1);
        if (src_fd < 0 || dst_fd < 0 ||
            fl_fs_read_file_in(src_fd, name, FL_SERVER_CATALOG_MAX_BYTES, &data, &len) != 0) {
            rc = FL_RESULT_ERR;
            break;
        }
        /* A blob is stored under its own digest: the name, the row's hash and the bytes must
         * agree, so a name already in the live store can only ever receive identical bytes. */
        if (!want_hash || strcmp(want_hash, name) != 0 ||
            fl_server_shared_sha256_hex(data, len, got_hash, sizeof(got_hash)) != FL_RESULT_OK ||
            strcmp(got_hash, name) != 0)
            rc = FL_RESULT_INVAL;
        else if (fl_fs_write_file_atomic_in(dst_fd, name, data, len) != 0)
            rc = FL_RESULT_ERR;
        mem_domain_free(MEM_DOMAIN_FS, data);
    }
    if (rc == FL_RESULT_OK && step != SQLITE_DONE)
        rc = FL_RESULT_ERR;
done:
    if (st)
        sqlite3_finalize(st);
    if (sql)
        sqlite3_close(sql);
    if (src_fd >= 0)
        close(src_fd);
    if (dst_fd >= 0)
        close(dst_fd);
    return rc;
}

fl_result_t fl_server_catalog_import_handoff(const char *src_server_shared_dir)
{
    char src_db[512];
    char dst_db[512];
    char dst_blobs[512];
    char tmp_db[600];
    int src_v, src_kq, dst_v, dst_kq;
    fl_result_t rc;

    if (!src_server_shared_dir || !src_server_shared_dir[0])
        return FL_RESULT_INVAL;
    if (fl_fs_join_path(src_db, sizeof(src_db), src_server_shared_dir,
                        FL_SERVER_CATALOG_DB_NAME) != 0)
        return FL_RESULT_ERR;
    if (catalog_db_path(dst_db, sizeof(dst_db)) != FL_RESULT_OK ||
        catalog_blobs_root(dst_blobs, sizeof(dst_blobs)) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (snprintf(tmp_db, sizeof(tmp_db), "%s.import.XXXXXX", dst_db) >= (int)sizeof(tmp_db))
        return FL_RESULT_ERR;
    (void)fl_server_catalog_close();
    if (mkdir_p_simple(g_cwd) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    {
        char shared[512];
        if (fl_fs_join_path(shared, sizeof(shared), g_cwd, FL_SERVER_SHARED_DIR_NAME) != 0 ||
            mkdir_p_simple(shared) != FL_RESULT_OK)
            return FL_RESULT_ERR;
    }
    /* A journal, WAL or shm file beside either database means a write is in flight or lives
     * outside the main file: refused, never copied torn or replayed onto the new file. */
    if (sidecar_present(src_db) || sidecar_present(dst_db))
        return FL_RESULT_ERR;
    /* The source is copied once; the copy is what gets probed, filled from and installed. */
    if (copy_source_to_temp(src_db, tmp_db) != FL_RESULT_OK)
        return FL_RESULT_ERR;
    if (catalog_file_probe(tmp_db, 0, &src_v, &src_kq) != FL_RESULT_OK ||
        catalog_file_probe(dst_db, 1, &dst_v, &dst_kq) != FL_RESULT_OK) {
        (void)unlink(tmp_db);
        return FL_RESULT_ERR;
    }
    if (src_v > FL_SERVER_CATALOG_SCHEMA_VERSION) {
        (void)unlink(tmp_db);
        return FL_RESULT_INVAL;
    }
    /* A policy refusal (not a transient one): waiting does not clear it. */
    if (dst_v > FL_SERVER_CATALOG_SCHEMA_VERSION) { /* never downgrade a newer live file */
        (void)unlink(tmp_db);
        return FL_RESULT_INVAL;
    }
    if (src_kq > 0 || dst_kq > 0) {
        (void)unlink(tmp_db);
        return FL_RESULT_ACCES;
    }
    rc = copy_blobs_listed_in(tmp_db, src_server_shared_dir, dst_blobs);
    if (rc != FL_RESULT_OK) {
        (void)unlink(tmp_db);
        return rc;
    }
    /* Look at the live file once more right before it is replaced. */
    if (catalog_file_probe(dst_db, 1, &dst_v, &dst_kq) != FL_RESULT_OK) {
        (void)unlink(tmp_db);
        return FL_RESULT_ERR;
    }
    if (dst_kq > 0) {
        (void)unlink(tmp_db);
        return FL_RESULT_ACCES;
    }
    if (rename(tmp_db, dst_db) != 0) {
        (void)unlink(tmp_db);
        return FL_RESULT_ERR;
    }
    return fl_server_catalog_open();
}
