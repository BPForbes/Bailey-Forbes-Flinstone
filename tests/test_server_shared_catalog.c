#include "contract_p5_server_catalog.h"
#include "server_shared_db.h"

#include "common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>

/* Every temp root made through mkdtemp_tracked is recorded and removed at exit (after leaving it);
 * the table holds the whole suite with room to spare, and a full table fails the test before a
 * root is created, so no root is left in /tmp by a run that reaches exit normally. */
#define S_TRACKED_MAX 128
static char s_tracked[S_TRACKED_MAX][64];
static int s_tracked_n;

static void cleanup_tracked(void)
{
    int i;
    if (chdir("/") != 0)
        return;
    for (i = 0; i < s_tracked_n; i++) {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", s_tracked[i]);
        (void)system(cmd);
    }
}

/* Fails (NULL) before a root is created when the table is full, so no root can go unrecorded. */
static char *mkdtemp_tracked(char *tmpl)
{
    char *r;
    if (s_tracked_n >= S_TRACKED_MAX) {
        fprintf(stderr, "FAIL: temp root table full (%d)\n", S_TRACKED_MAX);
        return NULL;
    }
    r = mkdtemp(tmpl);
    if (r) {
        snprintf(s_tracked[s_tracked_n++], sizeof(s_tracked[0]), "%s", r);
        if (s_tracked_n == 1)
            atexit(cleanup_tracked);
    }
    return r;
}

#define ASSERT(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", #c, __LINE__); return 1; } } while (0)

static int test_catalog_file_roundtrip(void)
{
    char tmp[] = "/tmp/fl_catalog_test_XXXXXX";
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "catalog blob payload";
    fl_server_catalog_entry_t entry;
    uint8_t out[64];
    size_t out_len = 0;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-catalog-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "note.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-catalog-1", &entry) == FL_RESULT_OK);
    ASSERT(entry.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(entry.payload_kind == FL_CHANNEL_PAYLOAD_FILE);
    snprintf(hash, sizeof(hash), "%s", entry.content_hash);

    ASSERT(fl_server_catalog_member_can_fetch(&entry, 3u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry, 4u, 0u) == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry, out, sizeof(out),
                                           &out_len) == FL_RESULT_OK);
    ASSERT(out_len == sizeof(payload) - 1u);
    ASSERT(memcmp(out, payload, out_len) == 0);

    (void)fl_server_catalog_close();
    return 0;
}

/* server_shared/ is trusted only as the user's own directory: a link in its place opens no catalog
 * and nothing is written through it; a loose mode on the user's own one is tightened to 0700. */
static int test_share_root_must_be_own_directory(void)
{
    char tmp[] = "/tmp/fl_catalog_root_XXXXXX";
    char shared[PATH_MAX], target[PATH_MAX], through[PATH_MAX];
    struct stat st;

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
    (void)fl_server_catalog_close();
    ASSERT(fl_fs_join_path(shared, sizeof(shared), tmp, FL_SERVER_SHARED_DIR_NAME) == 0);
    ASSERT(fl_fs_join_path(target, sizeof(target), tmp, "elsewhere") == 0);
    ASSERT(fl_fs_join_path(through, sizeof(through), target, FL_SERVER_CATALOG_DB_NAME) == 0);
    ASSERT(mkdir(target, 0700) == 0);
    ASSERT(symlink(target, shared) == 0);
    ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
    ASSERT(access(through, F_OK) != 0);

    ASSERT(unlink(shared) == 0);
    ASSERT(mkdir(shared, 0700) == 0 && chmod(shared, 0777) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    ASSERT(stat(shared, &st) == 0 && (st.st_mode & 07777) == 0700);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_msg_meta_pending_commit(void)
{
    char tmp[] = "/tmp/fl_catalog_mmeta_XXXXXX";
    const uint8_t body[] = "catalog via MSG_META pending row";
    fl_server_catalog_entry_t entry;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    ASSERT(fl_server_catalog_register_message_pending(
               "msg-meta-1", 2u, 0u, 0u, FL_FILE_FLAG_PUBLIC, "broadcast") ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_message("msg-meta-1", body,
                                            sizeof(body) - 1u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("msg-meta-1", &entry) == FL_RESULT_OK);
    ASSERT(entry.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(entry.payload_kind == FL_CHANNEL_PAYLOAD_MSG);
    snprintf(hash, sizeof(hash), "%s", entry.content_hash);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 5u, 0u, &entry, NULL, 0, NULL) ==
           FL_RESULT_OK);
    (void)fl_server_catalog_close();
    return 0;
}

static int transfer_id_looks_like_uuid_v4(const char *id)
{
    size_t n;
    if (!id)
        return 0;
    n = strlen(id);
    if (n != 36u)
        return 0;
    return id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-' &&
           id[14] == '4';
}

static int test_catalog_message_public(void)
{
    char tmp[] = "/tmp/fl_catalog_msg_XXXXXX";
    const uint8_t body[] = "hello catalog";
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char transfer_id[FL_SERVER_SHARE_ID_MAX];
    fl_server_catalog_entry_t entry;

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    ASSERT(fl_server_catalog_store_message(2u, 0u, "broadcast", body, sizeof(body) - 1u,
                                         0u, FL_FILE_FLAG_PUBLIC, transfer_id,
                                         sizeof(transfer_id), hash, sizeof(hash)) ==
           FL_RESULT_OK);
    ASSERT(transfer_id_looks_like_uuid_v4(transfer_id));
    ASSERT(fl_server_catalog_lookup_transfer(transfer_id, &entry) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 5u, 0u, &entry, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(entry.is_public == 1u);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_large_blob_fetch(void)
{
    char tmp[] = "/tmp/fl_catalog_large_XXXXXX";
    fl_server_file_offer_t offer;
    uint8_t payload[5000];
    fl_server_catalog_entry_t entry;
    uint8_t *out = NULL;
    size_t out_len = 0;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    size_t i;

    for (i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(i & 0xffu);

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-large-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "large.bin", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload)) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-large-1", &entry) == FL_RESULT_OK);
    snprintf(hash, sizeof(hash), "%s", entry.content_hash);

    out = (uint8_t *)malloc(sizeof(payload));
    ASSERT(out != NULL);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_read_blob(&entry, out, sizeof(payload), &out_len) ==
           FL_RESULT_OK);
    ASSERT(out_len == sizeof(payload));
    ASSERT(memcmp(out, payload, out_len) == 0);
    ASSERT(fl_server_catalog_read_blob(&entry, out, sizeof(payload) - 1u, &out_len) ==
           FL_RESULT_NOMEM);

    free(out);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_duplicate_hash_two_transfers(void)
{
    char tmp[] = "/tmp/fl_catalog_dup_XXXXXX";
    fl_server_file_offer_t offer_a;
    fl_server_file_offer_t offer_b;
    const uint8_t payload[] = "same bytes different transfer";
    fl_server_catalog_entry_t entry_a;
    fl_server_catalog_entry_t entry_b;
    char hash[FL_SERVER_CATALOG_HASH_HEX_MAX];

    ASSERT(mkdtemp_tracked(tmp) != NULL);
    if (chdir(tmp) != 0)
        return 1;
    strncpy(g_cwd, tmp, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';

    memset(&offer_a, 0, sizeof(offer_a));
    strncpy(offer_a.share_id, "share-dup-a", sizeof(offer_a.share_id) - 1u);
    strncpy(offer_a.file_name, "a.txt", sizeof(offer_a.file_name) - 1u);
    offer_a.sender_member_id = 2u;
    offer_a.receiver_member_id = 3u;
    offer_a.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    memset(&offer_b, 0, sizeof(offer_b));
    strncpy(offer_b.share_id, "share-dup-b", sizeof(offer_b.share_id) - 1u);
    strncpy(offer_b.file_name, "b.txt", sizeof(offer_b.file_name) - 1u);
    offer_b.sender_member_id = 4u;
    offer_b.receiver_member_id = 5u;
    offer_b.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;

    ASSERT(fl_server_catalog_register_file_offer(&offer_a) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer_a, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_register_file_offer(&offer_b) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer_b, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);

    ASSERT(fl_server_catalog_lookup_transfer("share-dup-a", &entry_a) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-dup-b", &entry_b) == FL_RESULT_OK);
    ASSERT(entry_a.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(entry_b.status == FL_SERVER_CATALOG_COMPLETE);
    ASSERT(strcmp(entry_a.content_hash, entry_b.content_hash) == 0);
    snprintf(hash, sizeof(hash), "%s", entry_a.content_hash);

    ASSERT(fl_server_catalog_member_can_fetch(&entry_a, 3u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry_b, 5u, 0u) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_member_can_fetch(&entry_a, 5u, 0u) == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 3u, 0u, &entry_a, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(strcmp(entry_a.transfer_id, "share-dup-a") == 0);
    ASSERT(fl_server_catalog_fetch_by_hash(hash, 5u, 0u, &entry_b, NULL, 0, NULL) ==
           FL_RESULT_OK);
    ASSERT(strcmp(entry_b.transfer_id, "share-dup-b") == 0);

    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_import_handoff(void)
{
    char src[] = "/tmp/fl_catalog_src_XXXXXX";
    char dst[] = "/tmp/fl_catalog_dst_XXXXXX";
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "handoff blob";
    fl_server_catalog_entry_t entry;

    ASSERT(mkdtemp_tracked(src) != NULL);
    ASSERT(mkdtemp_tracked(dst) != NULL);
    {
        char shared[512];
        snprintf(shared, sizeof(shared), "%s/server_shared", src);
        ASSERT(mkdir(shared, 0700) == 0 || errno == EEXIST);
        if (chdir(src) != 0)
            return 1;
        strncpy(g_cwd, src, sizeof(g_cwd) - 1u);
        g_cwd[sizeof(g_cwd) - 1u] = '\0';
    }
    memset(&offer, 0, sizeof(offer));
    strncpy(offer.share_id, "share-handoff-1", sizeof(offer.share_id) - 1u);
    strncpy(offer.file_name, "handoff.txt", sizeof(offer.file_name) - 1u);
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    (void)fl_server_catalog_close();

    {
        char shared[512];
        snprintf(shared, sizeof(shared), "%s/server_shared", dst);
        ASSERT(mkdir(shared, 0700) == 0 || errno == EEXIST);
        if (chdir(dst) != 0)
            return 1;
        strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
        g_cwd[sizeof(g_cwd) - 1u] = '\0';
    }
    {
        char src_shared[512];
        snprintf(src_shared, sizeof(src_shared), "%s/server_shared", src);
        ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    }
    ASSERT(fl_server_catalog_lookup_transfer("share-handoff-1", &entry) == FL_RESULT_OK);
    ASSERT(entry.status == FL_SERVER_CATALOG_COMPLETE);
    (void)fl_server_catalog_close();
    return 0;
}

/* ---- S1 hardening: reserved KQ kind, file mode, schema version, handoff ---- */

static int use_fresh_root(char *tmpl)
{
    if (!mkdtemp_tracked(tmpl) || chdir(tmpl) != 0)
        return 1;
    strncpy(g_cwd, tmpl, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
    return 0;
}

static int raw_exec(const char *root, const char *sql_text)
{
    char path[600];
    sqlite3 *db = NULL;
    int rc;
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", root);
    if (sqlite3_open(path, &db) != SQLITE_OK)
        return 1;
    rc = sqlite3_exec(db, sql_text, NULL, NULL, NULL);
    sqlite3_close(db);
    return rc == SQLITE_OK ? 0 : 1;
}

static int raw_status(const char *root, const char *id)
{
    char path[600];
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int v = -1;
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", root);
    if (sqlite3_open(path, &db) != SQLITE_OK)
        return -1;
    if (sqlite3_prepare_v2(db, "SELECT status FROM catalog_entry WHERE transfer_id=?;", -1, &st,
                           NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW)
            v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return v;
}

/* A row of the reserved kind that looks as servable as a row can: COMPLETE, public, receiver 0. */
#define KQ_ROW_SQL \
    "INSERT INTO catalog_entry(transfer_id,content_hash,payload_kind,sender_member_id," \
    "receiver_member_id,file_perms,is_public,created_at,expires_at,payload_name," \
    "storage_relpath,status,total_bytes) VALUES('kq-row-1','kqhash',3,2,0,0,1,1,0,'n.kqpb'," \
    "'blobs/kqhash',1,4);"

static int test_reserved_kind_not_served(void)
{
    char tmp[] = "/tmp/fl_catalog_kq_XXXXXX";
    char blob[700];
    fl_server_catalog_entry_t entry, meta, rows[8];
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "plain";
    uint8_t out[16];
    size_t len = 0, n = 0;
    FILE *fp;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, KQ_ROW_SQL) == 0);
    snprintf(blob, sizeof(blob), "%s/server_shared/blobs", tmp);
    ASSERT(mkdir(blob, 0700) == 0 || errno == EEXIST);
    snprintf(blob, sizeof(blob), "%s/server_shared/blobs/kqhash", tmp);
    fp = fopen(blob, "wb");
    ASSERT(fp != NULL);
    fwrite("kqkq", 1, 4, fp);
    fclose(fp);

    /* lookup refuses, and leaves the out struct empty */
    memset(&entry, 0xAB, sizeof(entry));
    ASSERT(fl_server_catalog_lookup_transfer("kq-row-1", &entry) == FL_RESULT_NOENT);
    ASSERT(entry.transfer_id[0] == '\0');

    /* a hand-built entry of that kind is refused by the gate and the byte reader */
    memset(&entry, 0, sizeof(entry));
    strcpy(entry.transfer_id, "kq-row-1");
    strcpy(entry.content_hash, "kqhash");
    strcpy(entry.storage_relpath, "blobs/kqhash");
    entry.payload_kind = FL_CHANNEL_PAYLOAD_KQ;
    entry.status = FL_SERVER_CATALOG_COMPLETE;
    entry.is_public = 1u;
    entry.total_bytes = 4u;
    ASSERT(fl_server_catalog_member_can_fetch(&entry, 9u, 0u) == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_read_blob(&entry, out, sizeof(out), &len) == FL_RESULT_ACCES);
    ASSERT(len == 0u);

    /* hash fetch and listings do not see it, with or without a data buffer */
    ASSERT(fl_server_catalog_fetch_by_hash("kqhash", 9u, 0u, &meta, out, sizeof(out), &len) ==
           FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_fetch_by_hash("kqhash", 9u, 0u, &meta, NULL, 0, NULL) ==
           FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_list_for_member(9u, 0u, rows, 8u, &n) == FL_RESULT_OK);
    ASSERT(n == 0u);

    /* a reserved id and a missing id are the same answer, and the row stays */
    ASSERT(fl_server_catalog_revoke_transfer("kq-row-1") == FL_RESULT_NOENT);
    ASSERT(raw_status(tmp, "kq-row-1") == (int)FL_SERVER_CATALOG_COMPLETE);
    ASSERT(fl_server_catalog_revoke_transfer("no-such-id") == FL_RESULT_NOENT);

    /* ordinary rows are unaffected beside it */
    memset(&offer, 0, sizeof(offer));
    strcpy(offer.share_id, "share-beside-kq");
    strcpy(offer.file_name, "a.txt");
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, sizeof(payload) - 1u) ==
           FL_RESULT_OK);
    ASSERT(fl_server_catalog_list_for_member(3u, 0u, rows, 8u, &n) == FL_RESULT_OK);
    ASSERT(n == 1u && strcmp(rows[0].transfer_id, "share-beside-kq") == 0);
    ASSERT(fl_server_catalog_revoke_transfer("share-beside-kq") == FL_RESULT_OK);
    ASSERT(raw_status(tmp, "share-beside-kq") == (int)FL_SERVER_CATALOG_REVOKED);
    ASSERT(fl_server_catalog_revoke_transfer("share-beside-kq") == FL_RESULT_OK);
    ASSERT(raw_status(tmp, "share-beside-kq") == (int)FL_SERVER_CATALOG_REVOKED);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_catalog_file_mode(void)
{
    char tmp[] = "/tmp/fl_catalog_mode_XXXXXX";
    char path[600];
    struct stat sb;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", tmp);
    ASSERT(stat(path, &sb) == 0);
    ASSERT((sb.st_mode & 0777) == 0600);
    (void)fl_server_catalog_close();

    /* an existing file with looser bits is tightened on open */
    ASSERT(chmod(path, 0644) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    ASSERT(stat(path, &sb) == 0);
    ASSERT((sb.st_mode & 0777) == 0600);
    (void)fl_server_catalog_close();
    return 0;
}

static int user_version_of(const char *root)
{
    char path[600];
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int v = -1;
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", root);
    if (sqlite3_open(path, &db) != SQLITE_OK)
        return -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return v;
}

static int test_catalog_schema_version(void)
{
    char tmp[] = "/tmp/fl_catalog_ver_XXXXXX";
    fl_server_catalog_entry_t entry;
    fl_server_file_offer_t offer;
    const uint8_t payload[] = "v";

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(user_version_of(tmp) == FL_SERVER_CATALOG_SCHEMA_VERSION);

    /* a catalog written before the stamp (version 0) keeps its rows and is stamped */
    memset(&offer, 0, sizeof(offer));
    strcpy(offer.share_id, "share-old");
    strcpy(offer.file_name, "o.txt");
    offer.sender_member_id = 2u;
    offer.receiver_member_id = 3u;
    offer.file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
    ASSERT(fl_server_catalog_register_file_offer(&offer) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&offer, payload, 1u) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, "PRAGMA user_version=0;") == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-old", &entry) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(user_version_of(tmp) == FL_SERVER_CATALOG_SCHEMA_VERSION);

    /* a newer file is refused and left as it was */
    ASSERT(raw_exec(tmp, "PRAGMA user_version=99;") == 0);
    ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("share-old", &entry) != FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(user_version_of(tmp) == 99);
    return 0;
}

/* FNV-1a over the whole file: "unchanged" means every byte, not only the size. */
static long long file_digest_of(const char *root)
{
    char path[600];
    unsigned char buf[4096];
    unsigned long long h = 1469598103934665603ULL;
    size_t n, i;
    FILE *fp;
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", root);
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0u)
        for (i = 0; i < n; i++)
            h = (h ^ buf[i]) * 1099511628211ULL;
    fclose(fp);
    return (long long)(h >> 1);
}

static int test_import_handoff_keeps_reserved_rows(void)
{
    char src[] = "/tmp/fl_catalog_hs_XXXXXX";
    char dst[] = "/tmp/fl_catalog_hd_XXXXXX";
    char src_shared[600];
    fl_server_catalog_entry_t entry;
    long long before;

    ASSERT(mkdtemp_tracked(src) != NULL && mkdtemp_tracked(dst) != NULL);
    snprintf(src_shared, sizeof(src_shared), "%s/server_shared", src);

    /* source: one ordinary catalog */
    strncpy(g_cwd, src, sizeof(g_cwd) - 1u);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    /* destination: holds a reserved row */
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(dst, KQ_ROW_SQL) == 0);
    before = file_digest_of(dst);
    ASSERT(before > 0);

    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ACCES);
    ASSERT(file_digest_of(dst) == before);
    ASSERT(raw_status(dst, "kq-row-1") == (int)FL_SERVER_CATALOG_COMPLETE);

    /* source holding a reserved row is refused too, and the destination is untouched */
    ASSERT(raw_exec(dst, "DELETE FROM catalog_entry;") == 0);
    before = file_digest_of(dst);
    ASSERT(raw_exec(src, KQ_ROW_SQL) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ACCES);
    ASSERT(file_digest_of(dst) == before);

    /* source from a newer schema is refused before anything is written */
    ASSERT(raw_exec(src, "DELETE FROM catalog_entry; PRAGMA user_version=99;") == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_INVAL);
    ASSERT(file_digest_of(dst) == before);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("none", &entry) == FL_RESULT_NOENT);

    /* a plain source is still imported */
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(src, "PRAGMA user_version=1;") == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    return 0;
}

/* ---- round 2: write paths, events, purge, kind range, file safety, concurrency, handoff ---- */

static int raw_int(const char *root, const char *query)
{
    char path[600];
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int v = -1;
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", root);
    if (sqlite3_open(path, &db) != SQLITE_OK)
        return -1;
    if (sqlite3_prepare_v2(db, query, -1, &st, NULL) == SQLITE_OK) {
        if (sqlite3_step(st) == SQLITE_ROW)
            v = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
    }
    sqlite3_close(db);
    return v;
}

static int make_shared_dir(const char *root)
{
    char p[600];
    snprintf(p, sizeof(p), "%s/server_shared", root);
    return (mkdir(p, 0700) == 0 || errno == EEXIST) ? 0 : 1;
}

static void offer_for(fl_server_file_offer_t *o, const char *id, uint16_t sender, uint16_t rcv,
                      const char *name)
{
    memset(o, 0, sizeof(*o));
    snprintf(o->share_id, sizeof(o->share_id), "%s", id);
    snprintf(o->file_name, sizeof(o->file_name), "%s", name);
    o->sender_member_id = sender;
    o->receiver_member_id = rcv;
    o->file_perms = FL_FILE_PERM_VIEW | FL_FILE_PERM_SERVER_SHARE;
}

#define KQ_STATE_Q "SELECT sender_member_id*1000+status*10+is_public FROM catalog_entry " \
                   "WHERE transfer_id='kq-row-1' AND payload_name='n.kqpb' AND content_hash='kqhash' " \
                   "AND storage_relpath='blobs/kqhash' AND payload_kind=3"

static int test_write_paths_refuse_reserved(void)
{
    char tmp[] = "/tmp/fl_catalog_wr_XXXXXX";
    fl_server_file_offer_t o;
    const uint8_t body[] = "evil";

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, KQ_ROW_SQL) == 0);
    ASSERT(raw_int(tmp, KQ_STATE_Q) == 2000 + 10 + 1);

    offer_for(&o, "kq-row-1", 9u, 3u, "evil");
    ASSERT(fl_server_catalog_register_file_offer(&o) == FL_RESULT_ACCES);
    ASSERT(raw_int(tmp, KQ_STATE_Q) == 2011);
    ASSERT(fl_server_catalog_commit_file_offer(&o, body, 4u) == FL_RESULT_ACCES);
    ASSERT(raw_int(tmp, KQ_STATE_Q) == 2011);
    ASSERT(fl_server_catalog_register_message_pending("kq-row-1", 9u, 3u, 0u, 0, "evil") ==
           FL_RESULT_ACCES);
    ASSERT(raw_int(tmp, KQ_STATE_Q) == 2011);
    ASSERT(fl_server_catalog_commit_message("kq-row-1", body, 4u) != FL_RESULT_OK);
    ASSERT(raw_int(tmp, KQ_STATE_Q) == 2011);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_acl WHERE transfer_id='kq-row-1'") == 0);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_event WHERE transfer_id='kq-row-1'") == 0);

    /* a message id reused as a file offer is a kind clash too, and changes nothing */
    ASSERT(fl_server_catalog_register_message_pending("m-1", 2u, 3u, 0u, 0, "note") ==
           FL_RESULT_OK);
    offer_for(&o, "m-1", 9u, 4u, "evil");
    ASSERT(fl_server_catalog_register_file_offer(&o) == FL_RESULT_ACCES);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_entry WHERE transfer_id='m-1' AND "
                        "sender_member_id=2 AND payload_name='note' AND payload_kind=2") == 1);

    /* ordinary behaviour is unchanged: re-register is an update, an unknown id a no-op */
    offer_for(&o, "plain-1", 2u, 3u, "a.txt");
    ASSERT(fl_server_catalog_register_file_offer(&o) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_register_file_offer(&o) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_commit_file_offer(&o, body, 4u) == FL_RESULT_OK);
    ASSERT(raw_status(tmp, "plain-1") == (int)FL_SERVER_CATALOG_COMPLETE);
    /* an id with no row: refused, and no history or grants are left under it */
    offer_for(&o, "unknown-1", 2u, 3u, "b.txt");
    ASSERT(fl_server_catalog_commit_file_offer(&o, body, 4u) == FL_RESULT_NOENT);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_acl WHERE transfer_id='unknown-1'") == 0);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_event WHERE transfer_id='unknown-1'") == 0);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_history_and_purge_skip_reserved(void)
{
    char tmp[] = "/tmp/fl_catalog_hp_XXXXXX";
    fl_server_catalog_event_t ev[4];
    size_t n = 9;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, KQ_ROW_SQL) == 0);
    ASSERT(raw_exec(tmp, "INSERT INTO catalog_event VALUES('kq-row-1',1,'seeded',0,1,'');") == 0);

    ASSERT(fl_server_catalog_append_event("kq-row-1", "offered", 2u, 5u, "") == FL_RESULT_ACCES);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_event WHERE transfer_id='kq-row-1'") == 1);
    ASSERT(fl_server_catalog_list_events("kq-row-1", ev, 4u, &n) == FL_RESULT_OK);
    ASSERT(n == 0u);
    /* ordinary ids, and ids with no row, keep working */
    ASSERT(fl_server_catalog_append_event("free-id", "offered", 2u, 5u, "x") == FL_RESULT_OK);
    ASSERT(fl_server_catalog_list_events("free-id", ev, 4u, &n) == FL_RESULT_OK);
    ASSERT(n == 1u);

    /* purge flips an expired ordinary row and leaves the reserved row to its owner */
    ASSERT(raw_exec(tmp, "UPDATE catalog_entry SET expires_at=5;"
                         "INSERT INTO catalog_entry VALUES('f-1','h',1,2,3,0,0,1,5,'a','',1,0);") == 0);
    ASSERT(fl_server_catalog_purge_expired(100u) == FL_RESULT_OK);
    ASSERT(raw_status(tmp, "f-1") == (int)FL_SERVER_CATALOG_EXPIRED);
    ASSERT(raw_status(tmp, "kq-row-1") == (int)FL_SERVER_CATALOG_COMPLETE);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_event WHERE transfer_id='kq-row-1' AND "
                        "event='expired'") == 0);
    ASSERT(raw_int(tmp, "SELECT COUNT(*) FROM catalog_event WHERE transfer_id='f-1' AND "
                        "event='expired'") == 1);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_kind_out_of_range_refused(void)
{
    char tmp[] = "/tmp/fl_catalog_kr_XXXXXX";
    fl_server_catalog_entry_t e;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp,
        "INSERT INTO catalog_entry VALUES('k257','h1',257,2,0,0,1,1,0,'a','',1,0);"
        "INSERT INTO catalog_entry VALUES('k259','h2',259,2,0,0,1,1,0,'a','',1,0);"
        "INSERT INTO catalog_entry VALUES('kbig','h3',4294967297,2,0,0,1,1,0,'a','',1,0);"
        "INSERT INTO catalog_entry VALUES('kneg','h4',-255,2,0,0,1,1,0,'a','',1,0);") == 0);
    ASSERT(fl_server_catalog_lookup_transfer("k257", &e) == FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_lookup_transfer("k259", &e) == FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_lookup_transfer("kbig", &e) == FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_lookup_transfer("kneg", &e) == FL_RESULT_NOENT);
    (void)fl_server_catalog_close();
    return 0;
}

static int test_open_refuses_links_and_other_names(void)
{
    char tmp[] = "/tmp/fl_catalog_ln_XXXXXX";
    char path[600], target[600], other[600];
    struct stat sb;
    int fd;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(make_shared_dir(tmp) == 0);
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", tmp);
    snprintf(target, sizeof(target), "%s/elsewhere.db", tmp);
    fd = open(target, O_CREAT | O_WRONLY, 0644);
    ASSERT(fd >= 0);
    close(fd);
    ASSERT(chmod(target, 0644) == 0);
    ASSERT(symlink(target, path) == 0);
    ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
    ASSERT(stat(target, &sb) == 0 && (sb.st_mode & 0777) == 0644);
    ASSERT(unlink(path) == 0);

    /* a second name for the file is refused as well */
    ASSERT(link(target, path) == 0);
    snprintf(other, sizeof(other), "%s/second-name", tmp);
    ASSERT(link(target, other) == 0);
    ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
    return 0;
}

static int test_schema_version_edge_cases(void)
{
    char tmp[] = "/tmp/fl_catalog_ve_XXXXXX";

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, "PRAGMA user_version=-1;") == 0);
    ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
    ASSERT(user_version_of(tmp) == -1);

    /* a file whose schema cannot be created is not stamped */
    {
        char tmp2[] = "/tmp/fl_catalog_vf_XXXXXX";
        ASSERT(use_fresh_root(tmp2) == 0);
        ASSERT(make_shared_dir(tmp2) == 0);
        ASSERT(raw_exec(tmp2, "CREATE VIEW catalog_entry AS SELECT 'x' AS transfer_id;") == 0);
        ASSERT(user_version_of(tmp2) == 0);
        ASSERT(fl_server_catalog_open() != FL_RESULT_OK);
        ASSERT(user_version_of(tmp2) == 0);
    }
    return 0;
}

static int test_concurrent_first_open(void)
{
    char tmp[] = "/tmp/fl_catalog_cc_XXXXXX";
    char path[600];
    int round;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(make_shared_dir(tmp) == 0);
    snprintf(path, sizeof(path), "%s/server_shared/host_catalog.db", tmp);
    for (round = 0; round < 15; round++) {
        enum { KIDS = 8 };
        pid_t pids[KIDS];
        int gate[2];
        int i, bad = 0;

        (void)unlink(path);
        ASSERT(raw_exec(tmp, "CREATE TABLE legacy_marker(x);") == 0); /* version 0, as before */
        ASSERT(pipe(gate) == 0);
        for (i = 0; i < KIDS; i++) {
            pids[i] = fork();
            ASSERT(pids[i] >= 0);
            if (pids[i] == 0) {
                char c;
                close(gate[1]);
                (void)read(gate[0], &c, 1); /* released together when the parent closes */
                _exit(fl_server_catalog_open() == FL_RESULT_OK ? 0 : 1);
            }
        }
        close(gate[0]);
        close(gate[1]);
        for (i = 0; i < KIDS; i++) {
            int st = 0;
            if (waitpid(pids[i], &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0)
                bad++;
        }
        ASSERT(bad == 0);
        ASSERT(user_version_of(tmp) == FL_SERVER_CATALOG_SCHEMA_VERSION);
    }
    return 0;
}

static int test_journal_is_owner_only(void)
{
    char tmp[] = "/tmp/fl_catalog_jr_XXXXXX";
    char jr[600];
    struct stat sb;
    mode_t old = umask(0);
    int rc = 1;

    if (use_fresh_root(tmp) != 0)
        goto out;
    if (fl_server_catalog_begin() != FL_RESULT_OK ||
        fl_server_catalog_append_event("j-1", "offered", 1u, 1u, "") != FL_RESULT_OK)
        goto out;
    snprintf(jr, sizeof(jr), "%s/server_shared/host_catalog.db-journal", tmp);
    if (stat(jr, &sb) != 0 || (sb.st_mode & 077) != 0)
        goto out;
    fl_server_catalog_rollback();
    rc = 0;
out:
    (void)fl_server_catalog_close();
    umask(old);
    if (rc)
        fprintf(stderr, "FAIL: journal mode\n");
    return rc;
}

static int make_catalog_with_share(const char *root, const char *id, const char *payload)
{
    fl_server_file_offer_t o;
    strncpy(g_cwd, root, sizeof(g_cwd) - 1u);
    g_cwd[sizeof(g_cwd) - 1u] = '\0';
    offer_for(&o, id, 2u, 3u, "f.txt");
    if (fl_server_catalog_register_file_offer(&o) != FL_RESULT_OK ||
        fl_server_catalog_commit_file_offer(&o, (const uint8_t *)payload, strlen(payload)) !=
            FL_RESULT_OK)
        return 1;
    (void)fl_server_catalog_close();
    return 0;
}

static int slurp(const char *path, char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp)
        return -1;
    n = fread(buf, 1, cap, fp);
    fclose(fp);
    return (int)n;
}

#include <dirent.h>
static int dir_file_count(const char *path)
{
    DIR *d;
    struct dirent *e;
    int n = 0;
    d = opendir(path);
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
            n++;
    }
    closedir(d);
    return n;
}

/* Temp files an import leaves behind in <root>/server_shared (any name starting host_catalog.db.import). */
static int import_leftover(const char *root)
{
    char dir[600];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(dir, sizeof(dir), "%s/server_shared", root);
    d = opendir(dir);
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "host_catalog.db.import", 22) == 0)
            n++;
    closedir(d);
    return n;
}

static int test_handoff_fails_closed(void)
{
    char src[] = "/tmp/fl_catalog_fs_XXXXXX";
    char dst[] = "/tmp/fl_catalog_fd_XXXXXX";
    char src_shared[600], dst_db[600], src_db[700], src_blob[700];
    static char before[65536], after[65536];
    fl_server_catalog_entry_t e;
    sqlite3 *lock = NULL;
    struct stat sb;
    int nb, na;

    ASSERT(mkdtemp_tracked(src) != NULL && mkdtemp_tracked(dst) != NULL);
    ASSERT(make_catalog_with_share(src, "from-src", "src bytes") == 0);
    ASSERT(make_catalog_with_share(dst, "from-dst", "dst bytes") == 0);
    snprintf(src_shared, sizeof(src_shared), "%s/server_shared", src);
    snprintf(src_db, sizeof(src_db), "%s/host_catalog.db", src_shared);
    snprintf(dst_db, sizeof(dst_db), "%s/server_shared/host_catalog.db", dst);
    nb = slurp(dst_db, before, sizeof(before));
    ASSERT(nb > 0 && nb < (int)sizeof(before));
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);

    /* destination locked by another connection: error, nothing written */
    ASSERT(sqlite3_open(dst_db, &lock) == SQLITE_OK);
    ASSERT(sqlite3_exec(lock, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(lock);
    na = slurp(dst_db, after, sizeof(after));
    ASSERT(na == nb && memcmp(before, after, (size_t)nb) == 0);
    ASSERT(import_leftover(dst) == 0);

    /* destination is not a database: error, bytes kept */
    {
        FILE *fp = fopen(dst_db, "wb");
        ASSERT(fp != NULL);
        fputs("this is not a sqlite database file, just text padding padding padding padding", fp);
        fclose(fp);
    }
    nb = slurp(dst_db, before, sizeof(before));
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    na = slurp(dst_db, after, sizeof(after));
    ASSERT(na == nb && memcmp(before, after, (size_t)nb) == 0);
    ASSERT(unlink(dst_db) == 0);
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
    ASSERT(make_catalog_with_share(dst, "from-dst", "dst bytes") == 0);
    nb = slurp(dst_db, before, sizeof(before));
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);

    /* source mid-write (a journal beside it): error, not a torn copy */
    ASSERT(sqlite3_open(src_db, &lock) == SQLITE_OK);
    ASSERT(sqlite3_exec(lock, "BEGIN; UPDATE catalog_entry SET total_bytes=total_bytes+1;", NULL, NULL,
                        NULL) == SQLITE_OK);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    sqlite3_exec(lock, "ROLLBACK;", NULL, NULL, NULL);
    sqlite3_close(lock);

    /* blob paths outside blobs/<name> are refused, wherever they point */
    {
        const char *bad[] = {"../escape", "/etc/hostname", "blobs/../escape", "blobs/a/b", "blobs/"};
        size_t i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            char q[200];
            snprintf(q, sizeof(q), "UPDATE catalog_entry SET storage_relpath='%s';", bad[i]);
            ASSERT(raw_exec(src, q) == 0);
            ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_INVAL);
            ASSERT(import_leftover(dst) == 0);
        }
    }
    na = slurp(dst_db, after, sizeof(after));
    ASSERT(na == nb && memcmp(before, after, (size_t)nb) == 0);

    /* a listed blob that is missing in the source is an error, not skipped */
    ASSERT(raw_exec(src, "UPDATE catalog_entry SET storage_relpath='blobs/missing-blob';") == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);

    /* a source catalog that is a link is refused */
    {
        char moved[720];
        snprintf(moved, sizeof(moved), "%s.real", src_db);
        ASSERT(rename(src_db, moved) == 0);
        ASSERT(symlink(moved, src_db) == 0);
        ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
        ASSERT(unlink(src_db) == 0);
        ASSERT(rename(moved, src_db) == 0);
    }

    /* the good case: installed owner-only, temp gone, blob copied owner-only */
    ASSERT(raw_exec(src, "UPDATE catalog_entry SET storage_relpath=(SELECT 'blobs/' || "
                         "content_hash FROM catalog_entry LIMIT 1);") == 0);
    snprintf(src_blob, sizeof(src_blob), "%s/blobs", src_shared);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    ASSERT(import_leftover(dst) == 0);
    ASSERT(stat(dst_db, &sb) == 0 && (sb.st_mode & 077) == 0);
    ASSERT(fl_server_catalog_lookup_transfer("from-src", &e) == FL_RESULT_OK);
    {
        uint8_t out[32];
        size_t len = 0;
        ASSERT(fl_server_catalog_read_blob(&e, out, sizeof(out), &len) == FL_RESULT_OK);
        ASSERT(len == 9u && memcmp(out, "src bytes", 9) == 0);
    }
    (void)fl_server_catalog_close();
    return 0;
}

/* ---- round 2b: catalogs we cannot write, and handoff edge files ---- */

/* Runs fn in a child that has dropped root (or is not root already); 0 = passed or skipped. */
static int run_unprivileged(int (*fn)(void))
{
    pid_t p = fork();
    int st = 0;
    if (p < 0)
        return 1;
    if (p == 0) {
        if (geteuid() == 0 && (setgid(65534) != 0 || setuid(65534) != 0))
            _exit(77);
        _exit(fn());
    }
    if (waitpid(p, &st, 0) < 0 || !WIFEXITED(st))
        return 1;
    return (WEXITSTATUS(st) == 0 || WEXITSTATUS(st) == 77) ? 0 : 1;
}

static int child_open_and_read(void)
{
    fl_server_catalog_entry_t e;
    if (fl_server_catalog_open() != FL_RESULT_OK)
        return 1;
    if (fl_server_catalog_lookup_transfer("ro-1", &e) != FL_RESULT_OK)
        return 2;
    return 0;
}

static int child_open_refused(void)
{
    return fl_server_catalog_open() != FL_RESULT_OK ? 0 : 1;
}

static int test_unwritable_catalog_is_read_only_or_refused(void)
{
    char tmp[] = "/tmp/fl_catalog_ro_XXXXXX";
    char dir[600], db[640];
    struct stat sb;

    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(raw_exec(tmp, "INSERT INTO catalog_entry VALUES('ro-1','h',1,2,0,0,1,1,0,'a','',1,0);"
                         "PRAGMA user_version=0;") == 0);
    snprintf(dir, sizeof(dir), "%s/server_shared", tmp);
    snprintf(db, sizeof(db), "%s/host_catalog.db", dir);
    ASSERT(chmod(tmp, 0755) == 0 && chmod(dir, 0755) == 0);

    /* write-protected and unstamped: the open follows KeyQuorum (restrict_owner_only): a file
     * with group or other bits is tightened, and when that is refused the open fails. This process
     * is root, so the child drops uid and does not own the file: 0444 has other bits, cannot be
     * tightened, and the open is refused with the mode and contents untouched. A non-root child
     * owns the file and tightens it (see the 0400 case below). */
    ASSERT(chmod(db, 0444) == 0);
    {
        long long d = file_digest_of(tmp);
        if (geteuid() == 0) {
            ASSERT(run_unprivileged(child_open_and_read) != 0);
            ASSERT(stat(db, &sb) == 0 && (sb.st_mode & 0777) == 0444);
            ASSERT(user_version_of(tmp) == 0);
            ASSERT(file_digest_of(tmp) == d);
        } else {
            ASSERT(chmod(db, 0400) == 0);
            ASSERT(run_unprivileged(child_open_and_read) == 0);
            ASSERT(stat(db, &sb) == 0 && (sb.st_mode & 0777) == 0600);
            ASSERT(user_version_of(tmp) == 0);
            ASSERT(file_digest_of(tmp) == d);
        }
    }
    /* readable by others and not ours to change: refused, mode and contents untouched. */
    if (geteuid() == 0) {
        ASSERT(chmod(db, 0644) == 0);
        ASSERT(run_unprivileged(child_open_and_read) != 0);
        ASSERT(stat(db, &sb) == 0 && (sb.st_mode & 0777) == 0644);
        ASSERT(user_version_of(tmp) == 0);
    }
    /* only a newer schema is refused, still without a write */
    ASSERT(chmod(db, 0644) == 0);
    ASSERT(raw_exec(tmp, "PRAGMA user_version=5;") == 0);
    ASSERT(run_unprivileged(child_open_refused) == 0);
    ASSERT(user_version_of(tmp) == 5);
    return 0;
}

static int fresh_pair(char *src, char *dst, char *src_shared, size_t cap, char *dst_db)
{
    if (!mkdtemp_tracked(src) || !mkdtemp_tracked(dst))
        return 1;
    if (make_catalog_with_share(src, "from-src", "src bytes") != 0 ||
        make_catalog_with_share(dst, "from-dst", "dst bytes") != 0)
        return 1;
    snprintf(src_shared, cap, "%s/server_shared", src);
    snprintf(dst_db, cap, "%s/server_shared/host_catalog.db", dst);
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
    return 0;
}

static int test_handoff_edge_files(void)
{
    char src[] = "/tmp/fl_catalog_es_XXXXXX";
    char dst[] = "/tmp/fl_catalog_ed_XXXXXX";
    char src_shared[600], dst_db[600];
    char src_db[640];
    fl_server_catalog_entry_t e;
    long long d;

    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    snprintf(src_db, sizeof(src_db), "%s/host_catalog.db", src_shared);
    d = file_digest_of(dst);

    /* a newer live file is never downgraded */
    ASSERT(raw_exec(dst, "PRAGMA user_version=5;") == 0);
    d = file_digest_of(dst);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_INVAL);
    ASSERT(user_version_of(dst) == 5);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(raw_exec(dst, "PRAGMA user_version=1;") == 0);

    /* a negative version on either side is an error */
    ASSERT(raw_exec(dst, "PRAGMA user_version=-1;") == 0);
    d = file_digest_of(dst);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(raw_exec(dst, "PRAGMA user_version=1;") == 0);
    d = file_digest_of(dst);
    ASSERT(raw_exec(src, "PRAGMA user_version=-1;") == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(raw_exec(src, "PRAGMA user_version=1;") == 0);

    /* a damaged file on either side is an error; the live file stays byte for byte */
    ASSERT(truncate(src_db, 2048) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) != FL_RESULT_OK);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(unlink(src_db) == 0);
    ASSERT(make_catalog_with_share(src, "from-src2", "src bytes") == 0);
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
    ASSERT(truncate(dst_db, 2048) == 0);
    d = file_digest_of(dst);
    ASSERT(fl_server_catalog_import_handoff(src_shared) != FL_RESULT_OK);
    ASSERT(file_digest_of(dst) == d);

    /* an empty file is a valid empty catalog on either side */
    ASSERT(truncate(dst_db, 0) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("from-src2", &e) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(truncate(src_db, 0) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("from-src2", &e) == FL_RESULT_NOENT);
    (void)fl_server_catalog_close();
    return 0;
}

/* ---- round 3 ---- */

static int test_handoff_refuses_wal_and_sidecars(void)
{
    char src[] = "/tmp/fl_catalog_w3s_XXXXXX";
    char dst[] = "/tmp/fl_catalog_w3d_XXXXXX";
    char src_shared[600], dst_db[600], src_db[700], side[760];
    sqlite3 *c = NULL;
    long long d;
    int fd;

    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    snprintf(src_db, sizeof(src_db), "%s/host_catalog.db", src_shared);
    d = file_digest_of(dst);

    /* a source in WAL mode whose newest rows exist only in the -wal file */
    ASSERT(sqlite3_open(src_db, &c) == SQLITE_OK);
    ASSERT(sqlite3_exec(c, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(c, "PRAGMA wal_autocheckpoint=0;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(c, KQ_ROW_SQL, NULL, NULL, NULL) == SQLITE_OK);
    snprintf(side, sizeof(side), "%s-wal", src_db);
    ASSERT(access(side, F_OK) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(import_leftover(dst) == 0);
    sqlite3_close(c);

    /* a stray sidecar of any kind beside the source, or beside the live file, is refused */
    {
        const char *sfx[] = {"-journal", "-wal", "-shm"};
        size_t i;
        for (i = 0; i < 3; i++) {
            snprintf(side, sizeof(side), "%s%s", src_db, sfx[i]);
            fd = open(side, O_CREAT | O_WRONLY, 0600);
            ASSERT(fd >= 0);
            close(fd);
            ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
            ASSERT(unlink(side) == 0);
            snprintf(side, sizeof(side), "%s%s", dst_db, sfx[i]);
            fd = open(side, O_CREAT | O_WRONLY, 0600);
            ASSERT(fd >= 0);
            close(fd);
            ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
            ASSERT(unlink(side) == 0);
        }
    }
    ASSERT(file_digest_of(dst) == d);
    return 0;
}

static int test_handoff_blob_integrity_and_dirs(void)
{
    char src[] = "/tmp/fl_catalog_b3s_XXXXXX";
    char dst[] = "/tmp/fl_catalog_b3d_XXXXXX";
    char src_shared[600], dst_db[600], live_blob[800], src_blob[800], q[900];
    char dst_hash[FL_SERVER_CATALOG_HASH_HEX_MAX];
    char buf[64];
    fl_server_catalog_entry_t e;
    long long d;
    FILE *fp;
    int n;

    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    ASSERT(fl_server_catalog_lookup_transfer("from-dst", &e) == FL_RESULT_OK);
    snprintf(dst_hash, sizeof(dst_hash), "%s", e.content_hash);
    (void)fl_server_catalog_close();
    snprintf(live_blob, sizeof(live_blob), "%s/server_shared/blobs/%s", dst, dst_hash);
    d = file_digest_of(dst);

    /* a source row that names a live blob but carries other bytes must not replace it */
    snprintf(src_blob, sizeof(src_blob), "%s/blobs/%s", src_shared, dst_hash);
    fp = fopen(src_blob, "wb");
    ASSERT(fp != NULL);
    fputs("evil bytes", fp);
    fclose(fp);
    snprintf(q, sizeof(q), "UPDATE catalog_entry SET content_hash='%s', storage_relpath='blobs/%s';",
             dst_hash, dst_hash);
    ASSERT(raw_exec(src, q) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_INVAL);
    n = slurp(live_blob, buf, sizeof(buf));
    ASSERT(n == 9 && memcmp(buf, "dst bytes", 9) == 0);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(import_leftover(dst) == 0);

    /* a row whose digest differs from its blob name is refused too */
    ASSERT(raw_exec(src, "UPDATE catalog_entry SET content_hash='not-the-name';") == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_INVAL);
    ASSERT(file_digest_of(dst) == d);

    return 0;
}

static int test_handoff_blob_dirs_not_links(void)
{
    char src[] = "/tmp/fl_catalog_l3s_XXXXXX";
    char dst[] = "/tmp/fl_catalog_l3d_XXXXXX";
    char src_shared[600], dst_db[600], blobs[700], real[760];
    fl_server_catalog_entry_t e;
    long long d;

    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    d = file_digest_of(dst);

    /* source blobs is a link to a directory holding the right files */
    snprintf(blobs, sizeof(blobs), "%s/blobs", src_shared);
    snprintf(real, sizeof(real), "%s.real", blobs);
    ASSERT(rename(blobs, real) == 0);
    ASSERT(symlink(real, blobs) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(unlink(blobs) == 0);
    ASSERT(rename(real, blobs) == 0);

    /* destination blobs is a link */
    snprintf(blobs, sizeof(blobs), "%s/server_shared/blobs", dst);
    snprintf(real, sizeof(real), "%s.real", blobs);
    ASSERT(rename(blobs, real) == 0);
    ASSERT(symlink(real, blobs) == 0);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
    ASSERT(file_digest_of(dst) == d);
    ASSERT(unlink(blobs) == 0);
    ASSERT(rename(real, blobs) == 0);

    /* pending offers store an empty storage_relpath. A blobs symlink still
     * fails closed, the live catalog is not replaced, and the link is not
     * replaced or written through. */
    ASSERT(raw_exec(src, "UPDATE catalog_entry SET storage_relpath='';") == 0);
    d = file_digest_of(dst);
    snprintf(blobs, sizeof(blobs), "%s/blobs", src_shared);
    snprintf(real, sizeof(real), "%s.real", blobs);
    ASSERT(rename(blobs, real) == 0);
    ASSERT(symlink(real, blobs) == 0);
    {
        int n = dir_file_count(real);
        ASSERT(n >= 0);
        ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
        ASSERT(file_digest_of(dst) == d);
        ASSERT(import_leftover(dst) == 0);
        ASSERT(dir_file_count(real) == n);
    }
    {
        struct stat lst;
        ASSERT(lstat(blobs, &lst) == 0 && S_ISLNK(lst.st_mode));
    }
    ASSERT(unlink(blobs) == 0);
    ASSERT(rename(real, blobs) == 0);

    snprintf(blobs, sizeof(blobs), "%s/server_shared/blobs", dst);
    snprintf(real, sizeof(real), "%s.real", blobs);
    ASSERT(rename(blobs, real) == 0);
    ASSERT(symlink(real, blobs) == 0);
    {
        int n = dir_file_count(real);
        ASSERT(n >= 0);
        ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ERR);
        ASSERT(file_digest_of(dst) == d);
        ASSERT(import_leftover(dst) == 0);
        ASSERT(dir_file_count(real) == n);
    }
    {
        struct stat lst;
        ASSERT(lstat(blobs, &lst) == 0 && S_ISLNK(lst.st_mode));
    }
    ASSERT(unlink(blobs) == 0);
    ASSERT(rename(real, blobs) == 0);
    ASSERT(raw_exec(src, "UPDATE catalog_entry SET storage_relpath='blobs/' || content_hash;") == 0);

    /* with real directories the same import goes through */
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    ASSERT(fl_server_catalog_lookup_transfer("from-src", &e) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    return 0;
}

#define NULLABLE_CATALOG_SQL \
    "CREATE TABLE catalog_entry(transfer_id TEXT PRIMARY KEY, content_hash TEXT NOT NULL," \
    "payload_kind INTEGER, sender_member_id INTEGER NOT NULL, receiver_member_id INTEGER NOT NULL " \
    "DEFAULT 0, file_perms INTEGER NOT NULL DEFAULT 0, is_public INTEGER NOT NULL DEFAULT 0, " \
    "created_at INTEGER NOT NULL, expires_at INTEGER NOT NULL DEFAULT 0, payload_name TEXT NOT NULL," \
    "storage_relpath TEXT NOT NULL, status INTEGER NOT NULL DEFAULT 0, total_bytes INTEGER NOT " \
    "NULL DEFAULT 0);" \
    "INSERT INTO catalog_entry VALUES('nk','h',NULL,2,0,0,1,1,5,'a','',1,0);"

static int test_null_kind_is_reserved(void)
{
    char tmp[] = "/tmp/fl_catalog_nk_XXXXXX";
    char src[] = "/tmp/fl_catalog_ns_XXXXXX";
    char dst[] = "/tmp/fl_catalog_nd_XXXXXX";
    char src_shared[600], dst_db[600];
    fl_server_catalog_event_t ev[2];
    fl_server_catalog_entry_t e;
    size_t n = 9;

    /* as the live file: history, lookup and purge treat the row as reserved */
    ASSERT(use_fresh_root(tmp) == 0);
    ASSERT(make_shared_dir(tmp) == 0);
    ASSERT(raw_exec(tmp, NULLABLE_CATALOG_SQL) == 0);
    ASSERT(fl_server_catalog_open() == FL_RESULT_OK);
    ASSERT(fl_server_catalog_append_event("nk", "offered", 2u, 5u, "") == FL_RESULT_ACCES);
    ASSERT(fl_server_catalog_list_events("nk", ev, 2u, &n) == FL_RESULT_OK && n == 0u);
    ASSERT(fl_server_catalog_lookup_transfer("nk", &e) == FL_RESULT_NOENT);
    ASSERT(fl_server_catalog_purge_expired(100u) == FL_RESULT_OK);
    ASSERT(raw_status(tmp, "nk") == (int)FL_SERVER_CATALOG_COMPLETE);
    ASSERT(fl_server_catalog_revoke_transfer("nk") == FL_RESULT_NOENT);
    ASSERT(raw_status(tmp, "nk") == (int)FL_SERVER_CATALOG_COMPLETE);
    ASSERT(fl_server_catalog_revoke_transfer("no-such-id") == FL_RESULT_NOENT);
    (void)fl_server_catalog_close();

    /* as an import source or destination it counts as a reserved row */
    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    (void)fl_server_catalog_close();
    ASSERT(unlink(dst_db) == 0);
    {
        char src_db[700];
        snprintf(src_db, sizeof(src_db), "%s/host_catalog.db", src_shared);
        ASSERT(unlink(src_db) == 0);
        ASSERT(raw_exec(src, NULLABLE_CATALOG_SQL) == 0);
    }
    strncpy(g_cwd, dst, sizeof(g_cwd) - 1u);
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_ACCES);
    return 0;
}

static int test_handoff_size_cap_and_temp_names(void)
{
    char src[] = "/tmp/fl_catalog_c3s_XXXXXX";
    char dst[] = "/tmp/fl_catalog_c3d_XXXXXX";
    char src_shared[600], dst_db[600], src_db[700], old_name[700], real[760];
    char buf[16];
    long long d;

    ASSERT(fresh_pair(src, dst, src_shared, sizeof(src_shared), dst_db) == 0);
    snprintf(src_db, sizeof(src_db), "%s/host_catalog.db", src_shared);
    d = file_digest_of(dst);

    /* a source over the cap is refused before anything is copied */
    snprintf(real, sizeof(real), "%s.real", src_db);
    ASSERT(rename(src_db, real) == 0);
    {
        int fd = open(src_db, O_CREAT | O_WRONLY, 0600);
        ASSERT(fd >= 0);
        ASSERT(ftruncate(fd, (off_t)FL_SERVER_CATALOG_MAX_DB_BYTES + 1) == 0);
        close(fd);
    }
    {
        /* the oversized sparse file goes before any assertion can return early */
        fl_result_t big = fl_server_catalog_import_handoff(src_shared);
        int gone = unlink(src_db);
        ASSERT(gone == 0);
        ASSERT(big == FL_RESULT_ERR);
    }
    ASSERT(file_digest_of(dst) == d);
    ASSERT(import_leftover(dst) == 0);
    ASSERT(rename(real, src_db) == 0);

    /* a file already sitting at the old fixed temp name is not another import's to remove */
    snprintf(old_name, sizeof(old_name), "%s.import.part", dst_db);
    {
        int fd = open(old_name, O_CREAT | O_WRONLY, 0600);
        ASSERT(fd >= 0);
        ASSERT(write(fd, "marker", 6) == 6);
        close(fd);
    }
    ASSERT(fl_server_catalog_import_handoff(src_shared) == FL_RESULT_OK);
    (void)fl_server_catalog_close();
    ASSERT(slurp(old_name, buf, sizeof(buf)) == 6);

    ASSERT(import_leftover(dst) == 1); /* only the marker file */
    return 0;
}

int main(void)
{
    if (test_catalog_file_roundtrip() != 0)
        return 1;
    if (test_catalog_msg_meta_pending_commit() != 0)
        return 1;
    if (test_catalog_message_public() != 0)
        return 1;
    if (test_catalog_large_blob_fetch() != 0)
        return 1;
    if (test_catalog_duplicate_hash_two_transfers() != 0)
        return 1;
    if (test_catalog_import_handoff() != 0)
        return 1;
    if (test_reserved_kind_not_served() != 0)
        return 1;
    if (test_catalog_file_mode() != 0)
        return 1;
    if (test_catalog_schema_version() != 0)
        return 1;
    if (test_import_handoff_keeps_reserved_rows() != 0)
        return 1;
    if (test_write_paths_refuse_reserved() != 0)
        return 1;
    if (test_history_and_purge_skip_reserved() != 0)
        return 1;
    if (test_kind_out_of_range_refused() != 0)
        return 1;
    if (test_open_refuses_links_and_other_names() != 0)
        return 1;
    if (test_schema_version_edge_cases() != 0)
        return 1;
    if (test_concurrent_first_open() != 0)
        return 1;
    if (test_journal_is_owner_only() != 0)
        return 1;
    if (test_handoff_fails_closed() != 0)
        return 1;
    if (test_share_root_must_be_own_directory() != 0)
        return 1;
    if (test_unwritable_catalog_is_read_only_or_refused() != 0)
        return 1;
    if (test_handoff_edge_files() != 0)
        return 1;
    if (test_handoff_refuses_wal_and_sidecars() != 0)
        return 1;
    if (test_handoff_blob_integrity_and_dirs() != 0)
        return 1;
    if (test_handoff_blob_dirs_not_links() != 0)
        return 1;
    if (test_null_kind_is_reserved() != 0)
        return 1;
    if (test_handoff_size_cap_and_temp_names() != 0)
        return 1;
    printf("All server_shared catalog tests passed.\n");
    return 0;
}
