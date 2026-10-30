#include "kq_delivery.h"
#include "server_shared_db.h"
#include "server_shared_fs.h"
#include "fl/hex.h"
#include "fl/sqlite_schema.h"
#include "mem_domain.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define KQ_STORE_LETTERS "kq_letters"
/* transfer_id of a delivery's catalog row: this prefix and the id in hex. */
#define KQ_TRANSFER_PREFIX "kq-"
#define KQ_TRANSFER_MAX (sizeof KQ_TRANSFER_PREFIX + 2 * FL_KQ_DELIVERY_ID_LEN)
/* Rows one leak pass ends; the rest wait for the next frame. */
#define KQ_LEAK_BATCH 64

struct fl_kq_store {
    sqlite3        *db;
    char            dir[1024];
    pthread_mutex_t lock;
};

/* ---- the neuron ---------------------------------------------------------- */

void fl_kq_delivery_init(fl_kq_delivery_t *d, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                         const char *sender, const char *recipient, const char *file_name,
                         const kq_file_offer_t *offer, int64_t now, int64_t ttl_sec) {
    memset(d, 0, sizeof *d);
    memcpy(d->id, id, FL_KQ_DELIVERY_ID_LEN);
    snprintf(d->sender, sizeof d->sender, "%s", sender ? sender : "");
    snprintf(d->recipient, sizeof d->recipient, "%s", recipient ? recipient : "");
    snprintf(d->file_name, sizeof d->file_name, "%s", file_name ? file_name : "");
    if (offer)
        d->offer = *offer;
    /* The author's verified signature is the first spike. */
    d->potential = 1u;
    d->threshold = FL_KQ_HOST_THRESHOLD;
    d->offered_at = now;
    d->expires_at = now + ttl_sec;
}

fl_kq_neuron_t fl_kq_delivery_integrate(fl_kq_delivery_t *d, fl_kq_spike_t spike,
                                        const uint8_t *cosig, int64_t now) {
    if (d->fired)
        return FL_KQ_NEURON_REFRACTORY;
    if (now >= d->expires_at)
        return FL_KQ_NEURON_LEAKED;
    if (spike == FL_KQ_SPIKE_COSIGN && cosig && !d->has_cosig) {
        memcpy(d->cosig, cosig, sizeof d->cosig);
        d->has_cosig = 1;
        d->potential++;
    }
    if (d->potential >= d->threshold) {
        d->fired = 1;
        return FL_KQ_NEURON_FIRE;
    }
    return FL_KQ_NEURON_REST;
}

/* ---- ids ------------------------------------------------------------------ */

void fl_kq_delivery_id_hex(const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                           char out[2 * FL_KQ_DELIVERY_ID_LEN + 1]) {
    fl_hex_encode(id, FL_KQ_DELIVERY_ID_LEN, out);
}

int fl_kq_delivery_id_parse(const char *hex, uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    return fl_hex_decode(hex, id, FL_KQ_DELIVERY_ID_LEN);
}

/* ---- the store ----------------------------------------------------------- */

/*
 * A delivery is one FL_CHANNEL_PAYLOAD_KQ row in the host catalog (catalog_entry, status HELD,
 * RELEASED, DELIVERED or EXPIRED) with its key ids and signed offer in catalog_kq and its history
 * in catalog_event. The share path never reads those rows. A row is never deleted: an expired or
 * delivered id stays taken, so the same offer cannot be held twice. Events name no account (the
 * actor is 0 and the detail "kq"); key ids live only in catalog_kq.
 */

static int letter_path(const fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                       char *out, size_t cap) {
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_delivery_id_hex(id, hex);
    return snprintf(out, cap, "%s/%s/%s.kqpb", s->dir, KQ_STORE_LETTERS, hex) < (int)cap ? 0 : -1;
}

static void transfer_of(const uint8_t id[FL_KQ_DELIVERY_ID_LEN], char out[KQ_TRANSFER_MAX]) {
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_delivery_id_hex(id, hex);
    snprintf(out, KQ_TRANSFER_MAX, KQ_TRANSFER_PREFIX "%s", hex);
}

static fl_result_t store_exec(fl_kq_store_t *s, const char *stmt) {
    return fl_sqlite_exec(s->db, "kq_store", stmt);
}

static fl_result_t log_event(fl_kq_store_t *s, const uint8_t *id, const char *what, int64_t at) {
    char transfer[KQ_TRANSFER_MAX];
    transfer_of(id, transfer);
    return fl_catalog_file_append_event(s->db, 1, transfer, what, 0, (uint64_t)at, "kq");
}

/* ---- deliveries held by an older build ------------------------------------ */

static void text_col(sqlite3_stmt *st, int col, char *out, size_t cap);
static fl_result_t set_state(fl_kq_store_t *s, const fl_kq_delivery_t *d, int from, int to,
                             int64_t released_at);

/* Where builds before the catalog kept deliveries: <dir>/kq/kq_delivery.db (schema 1) and
 * <dir>/kq/kq_letters/<id>.kqpb. */
#define KQ_LEGACY_DIR "kq"
#define KQ_LEGACY_DB "kq_delivery.db"
#define KQ_LEGACY_SCHEMA 1
/* Its spike kinds that map to catalog events (REQUEST and ANNOUNCE carried no state). */
static const char *legacy_event(int kind) {
    switch (kind) {
    case FL_KQ_SPIKE_OFFER: return "offered";
    case FL_KQ_SPIKE_COSIGN: return "cosigned";
    case FL_KQ_SPIKE_RELEASE: return "released";
    case FL_KQ_SPIKE_ACK: return "acknowledged";
    default: return NULL;
    }
}

static void legacy_say(const char *what) {
    fprintf(stderr, "kq_store: older deliveries not carried over: %s\n", what);
}

/* Reads one legacy letter by its plain name. Where the contained calls exist the letters
 * directory is held (letters_fd, opened without following a link) and a link at the name is
 * refused, so a link planted in the share tree cannot pull another file into a sealed letter;
 * a letters directory that could not be held reads nothing. Only where there are no *at calls
 * at all is the path used. Results follow fl_fs_read_file. */
static int legacy_read(int letters_fd, const char *legacy_letters, const char *name,
                       uint8_t **data, size_t *len) {
    char from[1200];

    if (letters_fd >= 0)
        return fl_fs_read_file_in(letters_fd, name, FL_KQ_LETTER_MAX, data, len);
    if (fl_fs_contained_available() ||
        fl_fs_join_path(from, sizeof from, legacy_letters, name) != 0) {
        if (data)
            *data = NULL;
        if (len)
            *len = 0;
        return -1;
    }
    return fl_fs_read_file(from, FL_KQ_LETTER_MAX, data, len);
}

/* One legacy row as a catalog row, its offer, its history, and (while it is live) its letter
 * copied into this store. The letter is written before the caller commits; *copied says so,
 * so a failed commit can remove it. */
static fl_result_t import_row(fl_kq_store_t *s, sqlite3 *old, sqlite3_stmt *row,
                              int letters_fd, const char *legacy_letters, int *copied) {
    fl_kq_delivery_t d;
    const void *blob;
    int fired, acked, status;
    int64_t released_at = 0;
    char transfer[KQ_TRANSFER_MAX];
    char hash[KQ_TRANSFER_MAX];
    char rel[64];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    char from[1200], to[1200];
    sqlite3_stmt *st = NULL;
    uint8_t *letter = NULL;
    size_t letter_len = 0;
    fl_result_t rc = FL_RESULT_ERR;
    int step;

    *copied = 0;
    memset(&d, 0, sizeof d);
    blob = sqlite3_column_blob(row, 0);
    if (!blob || sqlite3_column_bytes(row, 0) != FL_KQ_DELIVERY_ID_LEN)
        return FL_RESULT_INVAL;
    memcpy(d.id, blob, FL_KQ_DELIVERY_ID_LEN);
    text_col(row, 1, d.sender, sizeof d.sender);
    text_col(row, 2, d.recipient, sizeof d.recipient);
    text_col(row, 3, d.file_name, sizeof d.file_name);
    blob = sqlite3_column_blob(row, 4);
    if (!blob || sqlite3_column_bytes(row, 4) != (int)sizeof d.offer)
        return FL_RESULT_INVAL;
    memcpy(&d.offer, blob, sizeof d.offer);
    d.offered_at = sqlite3_column_int64(row, 5);
    d.expires_at = sqlite3_column_int64(row, 6);
    fired = sqlite3_column_int(row, 7);
    acked = sqlite3_column_int(row, 8);
    blob = sqlite3_column_blob(row, 9);
    if (blob && sqlite3_column_bytes(row, 9) == (int)sizeof d.cosig) {
        memcpy(d.cosig, blob, sizeof d.cosig);
        d.has_cosig = 1;
    }
    status = acked ? FL_SERVER_CATALOG_KQ_DELIVERED
                   : fired ? FL_SERVER_CATALOG_KQ_RELEASED : FL_SERVER_CATALOG_KQ_HELD;
    fl_kq_delivery_id_hex(d.id, hex);
    transfer_of(d.id, transfer);
    snprintf(hash, sizeof hash, "kq:%s", hex);
    snprintf(rel, sizeof rel, "%s/%s.kqpb", KQ_STORE_LETTERS, hex);

    /* The release time is the logged RELEASE spike (the offer time if none was logged). */
    if (fired) {
        released_at = d.offered_at;
        if (sqlite3_prepare_v2(old, "SELECT MAX(at) FROM kq_spike WHERE delivery_id=?1 AND kind=?2;",
                               -1, &st, NULL) == SQLITE_OK) {
            sqlite3_bind_blob(st, 1, d.id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
            sqlite3_bind_int(st, 2, FL_KQ_SPIKE_RELEASE);
            if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL)
                released_at = sqlite3_column_int64(st, 0);
        }
        sqlite3_finalize(st);
        st = NULL;
    }

    /* A taken id (imported before, or a new delivery) is left as it is. */
    if (sqlite3_prepare_v2(s->db,
                           "INSERT OR IGNORE INTO catalog_entry(transfer_id,content_hash,payload_kind,"
                           "sender_member_id,receiver_member_id,file_perms,is_public,created_at,"
                           "expires_at,payload_name,storage_relpath,status,total_bytes) "
                           "VALUES(?1,?2,3,0,0,0,0,?3,?4,?5,?6,?7,0);",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, d.offered_at);
    sqlite3_bind_int64(st, 4, d.expires_at);
    sqlite3_bind_text(st, 5, d.file_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, rel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, status);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    st = NULL;
    if (step != SQLITE_DONE)
        return FL_RESULT_ERR;
    if (sqlite3_changes(s->db) == 0)
        return FL_RESULT_OK;
    if (sqlite3_prepare_v2(s->db,
                           "INSERT INTO catalog_kq(transfer_id,delivery_id,sender_key,recipient_key,"
                           "offer,cosig,released_at) VALUES(?1,?2,?3,?4,?5,?6,?7);",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_text(st, 1, transfer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 2, d.id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d.sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, d.recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 5, &d.offer, (int)sizeof d.offer, SQLITE_TRANSIENT);
    if (d.has_cosig)
        sqlite3_bind_blob(st, 6, d.cosig, (int)sizeof d.cosig, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 6);
    sqlite3_bind_int64(st, 7, released_at);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    st = NULL;
    if (step != SQLITE_DONE)
        return FL_RESULT_ERR;

    /* Its history, in the order it was logged. */
    if (sqlite3_prepare_v2(old, "SELECT kind,at FROM kq_spike WHERE delivery_id=?1 ORDER BY seq;", -1,
                           &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_blob(st, 1, d.id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    rc = FL_RESULT_OK;
    while (rc == FL_RESULT_OK && (step = sqlite3_step(st)) == SQLITE_ROW) {
        const char *what = legacy_event(sqlite3_column_int(st, 0));
        if (what)
            rc = log_event(s, d.id, what, sqlite3_column_int64(st, 1));
    }
    sqlite3_finalize(st);
    if (rc != FL_RESULT_OK || step != SQLITE_DONE)
        return FL_RESULT_ERR;

    /* A delivered letter is gone already; a live one comes along (an unreadable one cannot be
     * released, so its row is ended now rather than left waiting). */
    if (status == FL_SERVER_CATALOG_KQ_DELIVERED)
        return FL_RESULT_OK;
    if (snprintf(from, sizeof from, "%s.kqpb", hex) >= (int)sizeof from ||
        letter_path(s, d.id, to, sizeof to) != 0)
        return FL_RESULT_ERR;
    if (legacy_read(letters_fd, legacy_letters, from, &letter, &letter_len) != 0 ||
        letter_len == 0) {
        mem_domain_free(MEM_DOMAIN_FS, letter);
        if (set_state(s, &d, status, FL_SERVER_CATALOG_EXPIRED, 0) != FL_RESULT_OK ||
            log_event(s, d.id, "expired", d.offered_at) != FL_RESULT_OK)
            return FL_RESULT_ERR;
        return FL_RESULT_OK;
    }
    rc = fl_fs_write_file_atomic(to, letter, letter_len) == 0 ? FL_RESULT_OK : FL_RESULT_ERR;
    mem_domain_free(MEM_DOMAIN_FS, letter);
    if (rc != FL_RESULT_OK)
        return FL_RESULT_ERR;
    *copied = 1;
    /* total_bytes is the letter's length, as fl_kq_store_put records it. */
    if (sqlite3_prepare_v2(s->db, "UPDATE catalog_entry SET total_bytes=?1 WHERE transfer_id=?2;", -1,
                           &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int64(st, 1, (sqlite3_int64)letter_len);
    sqlite3_bind_text(st, 2, transfer, -1, SQLITE_TRANSIENT);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    return step == SQLITE_DONE ? FL_RESULT_OK : FL_RESULT_ERR;
}

/*
 * Carries deliveries an older build held in <dir>/kq/kq_delivery.db into this store, once:
 * every row and its history in one transaction, live letters copied first and removed again if
 * that transaction does not commit. On success the old file is renamed kq_delivery.db.migrated,
 * so it is never read twice; on any failure it is left as it was and named on stderr. Ids this
 * store already holds are skipped. The old letters are left where they were.
 */
static void import_legacy(fl_kq_store_t *s) {
    char db[1200], letters[1200], done[1300], kq_dir[1200];
    uint8_t copied_ids[KQ_LEAK_BATCH][FL_KQ_DELIVERY_ID_LEN];
    size_t copied_n = 0;
    sqlite3 *old = NULL;
    sqlite3_stmt *row = NULL;
    int version = 0;
    int ok = 0;
    int step = SQLITE_DONE;
    int kq_fd = -1;
    int letters_fd = -1;
    struct stat sb;

    if (snprintf(db, sizeof db, "%s/%s/%s", s->dir, KQ_LEGACY_DIR, KQ_LEGACY_DB) >= (int)sizeof db ||
        snprintf(letters, sizeof letters, "%s/%s/%s", s->dir, KQ_LEGACY_DIR, KQ_STORE_LETTERS) >=
            (int)sizeof letters ||
        snprintf(done, sizeof done, "%s.migrated", db) >= (int)sizeof done ||
        snprintf(kq_dir, sizeof kq_dir, "%s/%s", s->dir, KQ_LEGACY_DIR) >= (int)sizeof kq_dir)
        return;
    if (lstat(db, &sb) != 0)
        return; /* nothing to carry over */
    if (!S_ISREG(sb.st_mode)) {
        legacy_say("kq/kq_delivery.db is not a regular file");
        return;
    }
    if (sqlite3_open_v2(db, &old, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        sqlite3_close(old);
        legacy_say("kq/kq_delivery.db cannot be opened");
        return;
    }
    if (sqlite3_prepare_v2(old, "PRAGMA user_version;", -1, &row, NULL) == SQLITE_OK &&
        sqlite3_step(row) == SQLITE_ROW)
        version = sqlite3_column_int(row, 0);
    sqlite3_finalize(row);
    row = NULL;
    if (version != KQ_LEGACY_SCHEMA) {
        sqlite3_close(old);
        legacy_say("kq/kq_delivery.db has an unknown schema version");
        return;
    }
    /* Both directories are held without following a link, once, and every later name is used
     * inside them: the letters are read and the old file is renamed through these handles, so
     * a link swapped in after the check above is not followed. (A directory that cannot be held
     * reads no letters; the row is then ended, as for any unreadable letter.) */
    kq_fd = fl_fs_dir_open_nofollow(kq_dir);
    letters_fd = fl_fs_dir_open_nofollow(letters);
    pthread_mutex_lock(&s->lock);
    if (store_exec(s, "BEGIN IMMEDIATE;") != FL_RESULT_OK)
        goto out;
    if (sqlite3_prepare_v2(old,
                           "SELECT delivery_id,sender,recipient,file_name,offer,offered_at,expires_at,"
                           "fired,acknowledged,cosig FROM kq_delivery ORDER BY offered_at;",
                           -1, &row, NULL) != SQLITE_OK)
        goto rollback;
    while ((step = sqlite3_step(row)) == SQLITE_ROW) {
        int copied = 0;
        if (copied_n == KQ_LEAK_BATCH)
            goto rollback; /* more live letters than one pass carries: leave it all for a person */
        if (import_row(s, old, row, letters_fd, letters, &copied) != FL_RESULT_OK) {
            if (copied)
                memcpy(copied_ids[copied_n++], sqlite3_column_blob(row, 0), FL_KQ_DELIVERY_ID_LEN);
            goto rollback;
        }
        if (copied)
            memcpy(copied_ids[copied_n++], sqlite3_column_blob(row, 0), FL_KQ_DELIVERY_ID_LEN);
    }
    if (step != SQLITE_DONE)
        goto rollback;
    sqlite3_finalize(row);
    row = NULL;
    if (store_exec(s, "COMMIT;") != FL_RESULT_OK)
        goto rollback;
    ok = 1;
    goto out;
rollback:
    (void)store_exec(s, "ROLLBACK;");
out:
    sqlite3_finalize(row);
    sqlite3_close(old);
    if (!ok) {
        for (size_t i = 0; i < copied_n; i++) {
            char path[1200];
            if (letter_path(s, copied_ids[i], path, sizeof path) == 0)
                (void)unlink(path);
        }
        legacy_say("the import failed; kq/kq_delivery.db is left as it was");
    } else if ((kq_fd >= 0 ? fl_fs_rename_in(kq_fd, KQ_LEGACY_DB, KQ_LEGACY_DB ".migrated")
                           : (fl_fs_contained_available() ? -1 : rename(db, done))) != 0) {
        legacy_say("imported, but kq/kq_delivery.db could not be renamed");
    }
    if (kq_fd >= 0)
        close(kq_fd);
    if (letters_fd >= 0)
        close(letters_fd);
    pthread_mutex_unlock(&s->lock);
}

fl_result_t fl_kq_store_open(const char *dir, fl_kq_store_t **out) {
    fl_kq_store_t *s;
    char path[1200];

    if (!dir || !out || strlen(dir) >= sizeof s->dir)
        return FL_RESULT_INVAL;
    *out = NULL;
    s = (fl_kq_store_t *)mem_domain_calloc(MEM_DOMAIN_FS, 1, sizeof *s);
    if (!s)
        return FL_RESULT_NOMEM;
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    /* The store may be the first thing under the share root: create it and its letters dir. */
    if (snprintf(path, sizeof path, "%s/%s", dir, KQ_STORE_LETTERS) >= (int)sizeof path ||
        fl_fs_mkdir_p(path, 0700) != 0) {
        mem_domain_free(MEM_DOMAIN_FS, s);
        return FL_RESULT_ERR;
    }
    snprintf(path, sizeof path, "%s/%s", dir, FL_SERVER_CATALOG_DB_NAME);
    if (fl_catalog_file_open(path, &s->db) != FL_RESULT_OK) {
        mem_domain_free(MEM_DOMAIN_FS, s);
        return FL_RESULT_ERR;
    }
    pthread_mutex_init(&s->lock, NULL);
    import_legacy(s);
    *out = s;
    return FL_RESULT_OK;
}

void fl_kq_store_close(fl_kq_store_t *s) {
    if (!s)
        return;
    sqlite3_close(s->db);
    pthread_mutex_destroy(&s->lock);
    mem_domain_free(MEM_DOMAIN_FS, s);
}

#define KQ_SELECT                                                                              \
    "SELECT k.delivery_id,k.sender_key,k.recipient_key,e.payload_name,k.offer,k.cosig,"         \
    "e.created_at,e.expires_at,e.status,k.released_at FROM catalog_entry e "                    \
    "JOIN catalog_kq k ON k.transfer_id=e.transfer_id WHERE e.payload_kind=3 "

static void text_col(sqlite3_stmt *st, int col, char *out, size_t cap) {
    const unsigned char *t = sqlite3_column_text(st, col);
    snprintf(out, cap, "%s", t ? (const char *)t : "");
}

static void row_to_delivery(sqlite3_stmt *st, fl_kq_delivery_t *d) {
    const void *blob;
    int status;

    memset(d, 0, sizeof *d);
    blob = sqlite3_column_blob(st, 0);
    if (blob && sqlite3_column_bytes(st, 0) == FL_KQ_DELIVERY_ID_LEN)
        memcpy(d->id, blob, FL_KQ_DELIVERY_ID_LEN);
    text_col(st, 1, d->sender, sizeof d->sender);
    text_col(st, 2, d->recipient, sizeof d->recipient);
    text_col(st, 3, d->file_name, sizeof d->file_name);
    blob = sqlite3_column_blob(st, 4);
    if (blob && sqlite3_column_bytes(st, 4) == (int)sizeof d->offer)
        memcpy(&d->offer, blob, sizeof d->offer);
    blob = sqlite3_column_blob(st, 5);
    if (blob && sqlite3_column_bytes(st, 5) == (int)sizeof d->cosig) {
        memcpy(d->cosig, blob, sizeof d->cosig);
        d->has_cosig = 1;
    }
    d->offered_at = sqlite3_column_int64(st, 6);
    d->expires_at = sqlite3_column_int64(st, 7);
    status = sqlite3_column_int(st, 8);
    /* The potential is the signatures held (the author's and a co-signature), never stored. */
    d->potential = 1u + (uint32_t)d->has_cosig;
    d->threshold = FL_KQ_HOST_THRESHOLD;
    d->fired = sqlite3_column_int64(st, 9) != 0;
    d->acknowledged = status == FL_SERVER_CATALOG_KQ_DELIVERED;
    d->expired = status == FL_SERVER_CATALOG_EXPIRED;
}

fl_result_t fl_kq_store_put(fl_kq_store_t *s, const fl_kq_delivery_t *d, const uint8_t *letter,
                            size_t letter_len) {
    static const char *const ins_entry =
        "INSERT INTO catalog_entry(transfer_id,content_hash,payload_kind,sender_member_id,"
        "receiver_member_id,file_perms,is_public,created_at,expires_at,payload_name,"
        "storage_relpath,status,total_bytes) VALUES(?1,?2,3,0,0,0,0,?3,?4,?5,?6,?7,?8);";
    static const char *const ins_kq =
        "INSERT INTO catalog_kq(transfer_id,delivery_id,sender_key,recipient_key,offer) "
        "VALUES(?1,?2,?3,?4,?5);";
    sqlite3_stmt *st = NULL;
    char path[1200];
    char transfer[KQ_TRANSFER_MAX];
    char hash[KQ_TRANSFER_MAX];
    char rel[64];
    fl_result_t rc = FL_RESULT_ERR;
    int step;

    if (!s || !d || !letter || letter_len == 0 || letter_len > FL_KQ_LETTER_MAX || d->fired)
        return FL_RESULT_INVAL;
    if (letter_path(s, d->id, path, sizeof path) != 0)
        return FL_RESULT_INVAL;
    transfer_of(d->id, transfer);
    /* content_hash is NOT NULL; a sealed letter is not content the host can hash meaningfully,
     * so the row names its delivery instead, which no SHA-256 hex can equal. */
    snprintf(hash, sizeof hash, "kq:%s", transfer + strlen(KQ_TRANSFER_PREFIX));
    snprintf(rel, sizeof rel, "%s/%s.kqpb", KQ_STORE_LETTERS, transfer + strlen(KQ_TRANSFER_PREFIX));
    pthread_mutex_lock(&s->lock);
    if (store_exec(s, "BEGIN IMMEDIATE;") != FL_RESULT_OK)
        goto out;
    if (sqlite3_prepare_v2(s->db, ins_entry, -1, &st, NULL) != SQLITE_OK)
        goto rollback;
    sqlite3_bind_text(st, 1, transfer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, d->offered_at);
    sqlite3_bind_int64(st, 4, d->expires_at);
    sqlite3_bind_text(st, 5, d->file_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, rel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 7, FL_SERVER_CATALOG_KQ_HELD);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)letter_len);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    st = NULL;
    if (step == SQLITE_CONSTRAINT) { /* the id is taken, by any row of any kind or state */
        rc = FL_RESULT_BUSY;
        goto rollback;
    }
    if (step != SQLITE_DONE || sqlite3_prepare_v2(s->db, ins_kq, -1, &st, NULL) != SQLITE_OK)
        goto rollback;
    sqlite3_bind_text(st, 1, transfer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 2, d->id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d->sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, d->recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 5, &d->offer, (int)sizeof d->offer, SQLITE_TRANSIENT);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    if (step == SQLITE_CONSTRAINT) {
        rc = FL_RESULT_BUSY;
        goto rollback;
    }
    if (step != SQLITE_DONE || log_event(s, d->id, "offered", d->offered_at) != FL_RESULT_OK)
        goto rollback;
    /* The letter is written before the row commits; a failed commit removes it. */
    if (fl_fs_write_file_atomic(path, letter, letter_len) != 0)
        goto rollback;
    if (store_exec(s, "COMMIT;") != FL_RESULT_OK) {
        unlink(path);
        goto rollback;
    }
    rc = FL_RESULT_OK;
    goto out;
rollback:
    (void)store_exec(s, "ROLLBACK;");
out:
    pthread_mutex_unlock(&s->lock);
    return rc;
}

fl_result_t fl_kq_store_get(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                            fl_kq_delivery_t *out) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_NOENT;

    if (!s || !id || !out)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db, KQ_SELECT "AND k.delivery_id=?;", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW) {
            row_to_delivery(st, out);
            rc = FL_RESULT_OK;
        }
    } else {
        rc = FL_RESULT_ERR;
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s->lock);
    return rc;
}

/* Sets a live row's status (and, on release, the co-signature and release time) only when it is
 * still in `from`; 0 rows changed means it moved on (another frame, or the leak). */
static fl_result_t set_state(fl_kq_store_t *s, const fl_kq_delivery_t *d, int from, int to,
                             int64_t released_at) {
    char transfer[KQ_TRANSFER_MAX];
    sqlite3_stmt *st = NULL;
    int ok;

    transfer_of(d->id, transfer);
    if (sqlite3_prepare_v2(s->db,
                           "UPDATE catalog_entry SET status=?1 WHERE transfer_id=?2 AND "
                           "payload_kind=3 AND status=?3;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_int(st, 1, to);
    sqlite3_bind_text(st, 2, transfer, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, from);
    ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(s->db) == 1;
    sqlite3_finalize(st);
    if (!ok)
        return FL_RESULT_NOENT;
    if (from != FL_SERVER_CATALOG_KQ_HELD || to == FL_SERVER_CATALOG_EXPIRED)
        return FL_RESULT_OK;
    if (sqlite3_prepare_v2(s->db, "UPDATE catalog_kq SET cosig=?1,released_at=?2 WHERE "
                                  "transfer_id=?3;",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    if (d->has_cosig)
        sqlite3_bind_blob(st, 1, d->cosig, (int)sizeof d->cosig, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 1);
    sqlite3_bind_int64(st, 2, released_at);
    sqlite3_bind_text(st, 3, transfer, -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(s->db) == 1;
    sqlite3_finalize(st);
    return ok ? FL_RESULT_OK : FL_RESULT_ERR;
}

fl_result_t fl_kq_store_update(fl_kq_store_t *s, const fl_kq_delivery_t *d, fl_kq_spike_t spike,
                               int64_t now) {
    fl_result_t rc;

    if (!s || !d)
        return FL_RESULT_INVAL;
    if (spike != FL_KQ_SPIKE_COSIGN && !(spike == FL_KQ_SPIKE_ACK && d->acknowledged))
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s->lock);
    rc = store_exec(s, "BEGIN IMMEDIATE;");
    if (rc != FL_RESULT_OK)
        goto out;
    if (spike == FL_KQ_SPIKE_ACK) {
        rc = set_state(s, d, FL_SERVER_CATALOG_KQ_RELEASED, FL_SERVER_CATALOG_KQ_DELIVERED, 0);
        if (rc == FL_RESULT_OK)
            rc = log_event(s, d->id, "acknowledged", now);
    } else {
        /* A co-signature that fires is one unit of work: the signature, the release and both
         * events commit together or not at all. */
        rc = set_state(s, d, FL_SERVER_CATALOG_KQ_HELD,
                       d->fired ? FL_SERVER_CATALOG_KQ_RELEASED : FL_SERVER_CATALOG_KQ_HELD,
                       d->fired ? now : 0);
        if (rc == FL_RESULT_OK)
            rc = log_event(s, d->id, "cosigned", now);
        if (rc == FL_RESULT_OK && d->fired)
            rc = log_event(s, d->id, "released", now);
    }
    if (rc == FL_RESULT_OK)
        rc = store_exec(s, "COMMIT;");
    if (rc != FL_RESULT_OK)
        (void)store_exec(s, "ROLLBACK;");
out:
    pthread_mutex_unlock(&s->lock);
    return rc;
}

fl_result_t fl_kq_store_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               uint8_t **letter, size_t *len) {
    char path[1200];
    int rc;

    if (!s || !id || !letter || !len)
        return FL_RESULT_INVAL;
    *letter = NULL;
    *len = 0;
    if (letter_path(s, id, path, sizeof path) != 0)
        return FL_RESULT_INVAL;
    rc = fl_fs_read_file(path, FL_KQ_LETTER_MAX, letter, len);
    if (rc == -3)
        return FL_RESULT_NOENT;
    if (rc != 0)
        return FL_RESULT_ERR;
    if (*len == 0u) { /* an empty letter is not a letter */
        mem_domain_free(MEM_DOMAIN_FS, *letter);
        *letter = NULL;
        return FL_RESULT_ERR;
    }
    return FL_RESULT_OK;
}

fl_result_t fl_kq_store_forget_letter(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    char path[1200];

    if (!s || !id || letter_path(s, id, path, sizeof path) != 0)
        return FL_RESULT_INVAL;
    return (unlink(path) == 0 || errno == ENOENT) ? FL_RESULT_OK : FL_RESULT_ERR;
}

/* Live: held before its expiry, or released and not yet past the time it waits for a receipt. */
#define KQ_LIVE_SQL(cmp)                                                                       \
    "((e.status=4 AND e.expires_at" cmp "?2) OR (e.status=5 AND k.released_at+" KQ_REL_TTL     \
    cmp "?2))"
#define KQ_STR(x) #x
#define KQ_XSTR(x) KQ_STR(x)
#define KQ_REL_TTL KQ_XSTR(FL_KQ_RELEASE_TTL_SEC)

size_t fl_kq_store_pending_for(fl_kq_store_t *s, const char *recipient, int64_t now,
                               fl_kq_delivery_t *out, size_t cap) {
    sqlite3_stmt *st = NULL;
    size_t n = 0;

    if (!s || !recipient || !out || cap == 0)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db,
                           KQ_SELECT "AND k.recipient_key=?1 AND " KQ_LIVE_SQL(">")
                           " ORDER BY e.created_at;",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, recipient, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(st, 2, now);
        while (n < cap && sqlite3_step(st) == SQLITE_ROW)
            row_to_delivery(st, &out[n++]);
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s->lock);
    return n;
}

size_t fl_kq_store_sent_by(fl_kq_store_t *s, const char *sender, fl_kq_delivery_t *out,
                           size_t cap) {
    sqlite3_stmt *st = NULL;
    size_t n = 0;

    if (!s || !sender || !out || cap == 0)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db, KQ_SELECT "AND k.sender_key=?1 ORDER BY e.created_at DESC;", -1,
                           &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, sender, -1, SQLITE_TRANSIENT);
        while (n < cap && sqlite3_step(st) == SQLITE_ROW)
            row_to_delivery(st, &out[n++]);
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s->lock);
    return n;
}

const char *fl_kq_delivery_state(const fl_kq_delivery_t *d) {
    if (!d)
        return "";
    if (d->expired)
        return "expired";
    if (d->acknowledged)
        return "delivered";
    return d->fired ? "released" : "waiting for co-signature";
}

size_t fl_kq_store_leak(fl_kq_store_t *s, int64_t now) {
    sqlite3_stmt *st = NULL;
    uint8_t ids[KQ_LEAK_BATCH][FL_KQ_DELIVERY_ID_LEN];
    int from[KQ_LEAK_BATCH];
    size_t n = 0;

    if (!s)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db,
                           "SELECT k.delivery_id,e.status FROM catalog_entry e JOIN catalog_kq k "
                           "ON k.transfer_id=e.transfer_id WHERE e.payload_kind=3 AND "
                           KQ_LIVE_SQL("<=") " LIMIT " KQ_XSTR(KQ_LEAK_BATCH) ";",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 2, now);
        while (n < KQ_LEAK_BATCH && sqlite3_step(st) == SQLITE_ROW) {
            if (sqlite3_column_bytes(st, 0) == FL_KQ_DELIVERY_ID_LEN) {
                memcpy(ids[n], sqlite3_column_blob(st, 0), FL_KQ_DELIVERY_ID_LEN);
                from[n++] = sqlite3_column_int(st, 1);
            }
        }
    }
    sqlite3_finalize(st);
    if (n == 0)
        goto out;
    /* The rows and their "expired" events first, in one transaction; the letters only once that
     * has committed, so a failure leaves every delivery as it was. The rows stay: an expired id
     * is never held again. */
    if (store_exec(s, "BEGIN IMMEDIATE;") != FL_RESULT_OK) {
        n = 0;
        goto out;
    }
    for (size_t i = 0; i < n; i++) {
        fl_kq_delivery_t d;
        memset(&d, 0, sizeof d);
        memcpy(d.id, ids[i], FL_KQ_DELIVERY_ID_LEN);
        if (set_state(s, &d, from[i], FL_SERVER_CATALOG_EXPIRED, 0) != FL_RESULT_OK ||
            log_event(s, ids[i], "expired", now) != FL_RESULT_OK) {
            (void)store_exec(s, "ROLLBACK;");
            n = 0;
            goto out;
        }
    }
    if (store_exec(s, "COMMIT;") != FL_RESULT_OK) {
        (void)store_exec(s, "ROLLBACK;");
        n = 0;
        goto out;
    }
    for (size_t i = 0; i < n; i++) {
        char path[1200];
        if (letter_path(s, ids[i], path, sizeof path) == 0)
            (void)unlink(path);
    }
out:
    pthread_mutex_unlock(&s->lock);
    return n;
}

fl_result_t fl_kq_store_events(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                               fl_server_catalog_event_t *out, size_t cap, size_t *count) {
    char transfer[KQ_TRANSFER_MAX];
    fl_result_t rc;

    if (count)
        *count = 0;
    if (!s || !id)
        return FL_RESULT_INVAL;
    transfer_of(id, transfer);
    pthread_mutex_lock(&s->lock);
    rc = fl_catalog_file_list_events(s->db, 1, transfer, out, cap, count);
    pthread_mutex_unlock(&s->lock);
    return rc;
}
