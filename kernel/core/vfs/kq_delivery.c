#include "kq_delivery.h"
#include "server_shared_fs.h"
#include "fl/hex.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define KQ_STORE_SCHEMA_VERSION 1
#define KQ_STORE_DB "kq_delivery.db"
#define KQ_STORE_LETTERS "kq_letters"

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

static int letter_path(const fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN],
                       char *out, size_t cap) {
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_kq_delivery_id_hex(id, hex);
    return snprintf(out, cap, "%s/%s/%s.kqpb", s->dir, KQ_STORE_LETTERS, hex) < (int)cap ? 0 : -1;
}

static fl_result_t migrate(sqlite3 *db) {
    sqlite3_stmt *st = NULL;
    int version = 0;

    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    if (sqlite3_step(st) == SQLITE_ROW)
        version = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    if (version > KQ_STORE_SCHEMA_VERSION)
        return FL_RESULT_INVAL; /* written by a newer build */
    if (version < 1) {
        static const char *v1 =
            "BEGIN;"
            "CREATE TABLE IF NOT EXISTS kq_delivery ("
            "  delivery_id BLOB PRIMARY KEY NOT NULL,"
            "  sender TEXT NOT NULL, recipient TEXT NOT NULL, file_name TEXT NOT NULL,"
            "  offer BLOB NOT NULL, potential INTEGER NOT NULL, threshold INTEGER NOT NULL,"
            "  offered_at INTEGER NOT NULL, expires_at INTEGER NOT NULL,"
            "  fired INTEGER NOT NULL DEFAULT 0, acknowledged INTEGER NOT NULL DEFAULT 0,"
            "  cosig BLOB);"
            "CREATE TABLE IF NOT EXISTS kq_spike ("
            "  delivery_id BLOB NOT NULL, seq INTEGER NOT NULL, kind INTEGER NOT NULL,"
            "  actor TEXT NOT NULL, at INTEGER NOT NULL, PRIMARY KEY (delivery_id, seq));"
            "CREATE INDEX IF NOT EXISTS kq_delivery_pending"
            "  ON kq_delivery (recipient, acknowledged, offered_at);"
            "CREATE INDEX IF NOT EXISTS kq_delivery_expiry ON kq_delivery (fired, expires_at);"
            "PRAGMA user_version = 1;"
            "COMMIT;";
        if (sqlite3_exec(db, v1, NULL, NULL, NULL) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return FL_RESULT_ERR;
        }
    }
    return FL_RESULT_OK;
}

fl_result_t fl_kq_store_open(const char *dir, fl_kq_store_t **out) {
    fl_kq_store_t *s;
    char path[1200];

    if (!dir || !out || strlen(dir) >= sizeof s->dir)
        return FL_RESULT_INVAL;
    *out = NULL;
    s = (fl_kq_store_t *)calloc(1, sizeof *s);
    if (!s)
        return FL_RESULT_NOMEM;
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    /* The store may be the first thing under the share root: create it and its letters dir. */
    if (snprintf(path, sizeof path, "%s/%s", dir, KQ_STORE_LETTERS) >= (int)sizeof path ||
        fl_fs_mkdir_p(path, 0700) != 0) {
        free(s);
        return FL_RESULT_ERR;
    }
    snprintf(path, sizeof path, "%s/%s", dir, KQ_STORE_DB);
    if (sqlite3_open(path, &s->db) != SQLITE_OK || migrate(s->db) != FL_RESULT_OK) {
        sqlite3_close(s->db);
        free(s);
        return FL_RESULT_ERR;
    }
    (void)chmod(path, 0600);
    pthread_mutex_init(&s->lock, NULL);
    *out = s;
    return FL_RESULT_OK;
}

void fl_kq_store_close(fl_kq_store_t *s) {
    if (!s)
        return;
    sqlite3_close(s->db);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

static void row_to_delivery(sqlite3_stmt *st, fl_kq_delivery_t *d) {
    const void *blob;

    memset(d, 0, sizeof *d);
    blob = sqlite3_column_blob(st, 0);
    if (blob && sqlite3_column_bytes(st, 0) == FL_KQ_DELIVERY_ID_LEN)
        memcpy(d->id, blob, FL_KQ_DELIVERY_ID_LEN);
    snprintf(d->sender, sizeof d->sender, "%s", (const char *)sqlite3_column_text(st, 1));
    snprintf(d->recipient, sizeof d->recipient, "%s", (const char *)sqlite3_column_text(st, 2));
    snprintf(d->file_name, sizeof d->file_name, "%s", (const char *)sqlite3_column_text(st, 3));
    blob = sqlite3_column_blob(st, 4);
    if (blob && sqlite3_column_bytes(st, 4) == (int)sizeof d->offer)
        memcpy(&d->offer, blob, sizeof d->offer);
    d->potential = (uint32_t)sqlite3_column_int(st, 5);
    d->threshold = (uint32_t)sqlite3_column_int(st, 6);
    d->offered_at = sqlite3_column_int64(st, 7);
    d->expires_at = sqlite3_column_int64(st, 8);
    d->fired = sqlite3_column_int(st, 9);
    d->acknowledged = sqlite3_column_int(st, 10);
    blob = sqlite3_column_blob(st, 11);
    if (blob && sqlite3_column_bytes(st, 11) == (int)sizeof d->cosig) {
        memcpy(d->cosig, blob, sizeof d->cosig);
        d->has_cosig = 1;
    }
}

#define KQ_COLUMNS                                                                           \
    "delivery_id,sender,recipient,file_name,offer,potential,threshold,offered_at,expires_at," \
    "fired,acknowledged,cosig"

static fl_result_t log_spike(fl_kq_store_t *s, const uint8_t *id, fl_kq_spike_t spike,
                             const char *actor, int64_t now) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (sqlite3_prepare_v2(s->db,
                           "INSERT INTO kq_spike(delivery_id,seq,kind,actor,at) VALUES(?1,"
                           "(SELECT COALESCE(MAX(seq),0)+1 FROM kq_spike WHERE delivery_id=?1),"
                           "?2,?3,?4);",
                           -1, &st, NULL) != SQLITE_OK)
        return FL_RESULT_ERR;
    sqlite3_bind_blob(st, 1, id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, (int)spike);
    sqlite3_bind_text(st, 3, actor ? actor : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 4, now);
    if (sqlite3_step(st) == SQLITE_DONE)
        rc = FL_RESULT_OK;
    sqlite3_finalize(st);
    return rc;
}

fl_result_t fl_kq_store_put(fl_kq_store_t *s, const fl_kq_delivery_t *d, const uint8_t *letter,
                            size_t letter_len) {
    sqlite3_stmt *st = NULL;
    char path[1200];
    fl_result_t rc = FL_RESULT_ERR;
    int step;

    if (!s || !d || !letter || letter_len == 0 || letter_len > FL_KQ_LETTER_MAX)
        return FL_RESULT_INVAL;
    if (letter_path(s, d->id, path, sizeof path) != 0)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_exec(s->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
        goto out;
    if (sqlite3_prepare_v2(s->db,
                           "INSERT INTO kq_delivery(" KQ_COLUMNS ") "
                           "VALUES(?,?,?,?,?,?,?,?,?,?,?,NULL);",
                           -1, &st, NULL) != SQLITE_OK)
        goto rollback;
    sqlite3_bind_blob(st, 1, d->id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, d->sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d->recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, d->file_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(st, 5, &d->offer, (int)sizeof d->offer, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 6, (int)d->potential);
    sqlite3_bind_int(st, 7, (int)d->threshold);
    sqlite3_bind_int64(st, 8, d->offered_at);
    sqlite3_bind_int64(st, 9, d->expires_at);
    sqlite3_bind_int(st, 10, d->fired);
    sqlite3_bind_int(st, 11, d->acknowledged);
    step = sqlite3_step(st);
    sqlite3_finalize(st);
    if (step == SQLITE_CONSTRAINT) {
        rc = FL_RESULT_BUSY;
        goto rollback;
    }
    if (step != SQLITE_DONE || log_spike(s, d->id, FL_KQ_SPIKE_OFFER, d->sender, d->offered_at) !=
                                   FL_RESULT_OK)
        goto rollback;
    /* The letter is written before the row commits; a failed commit removes it. */
    if (fl_fs_write_file_atomic(path, letter, letter_len) != 0)
        goto rollback;
    if (sqlite3_exec(s->db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        unlink(path);
        goto rollback;
    }
    rc = FL_RESULT_OK;
    goto out;
rollback:
    sqlite3_exec(s->db, "ROLLBACK;", NULL, NULL, NULL);
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
    if (sqlite3_prepare_v2(s->db, "SELECT " KQ_COLUMNS " FROM kq_delivery WHERE delivery_id=?;",
                           -1, &st, NULL) == SQLITE_OK) {
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

fl_result_t fl_kq_store_update(fl_kq_store_t *s, const fl_kq_delivery_t *d, fl_kq_spike_t spike,
                               const char *actor, int64_t now) {
    sqlite3_stmt *st = NULL;
    fl_result_t rc = FL_RESULT_ERR;

    if (!s || !d)
        return FL_RESULT_INVAL;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_exec(s->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
        goto out;
    if (sqlite3_prepare_v2(s->db,
                           "UPDATE kq_delivery SET potential=?,fired=?,acknowledged=?,cosig=? "
                           "WHERE delivery_id=?;",
                           -1, &st, NULL) != SQLITE_OK)
        goto rollback;
    sqlite3_bind_int(st, 1, (int)d->potential);
    sqlite3_bind_int(st, 2, d->fired);
    sqlite3_bind_int(st, 3, d->acknowledged);
    if (d->has_cosig)
        sqlite3_bind_blob(st, 4, d->cosig, (int)sizeof d->cosig, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(st, 4);
    sqlite3_bind_blob(st, 5, d->id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE || sqlite3_changes(s->db) != 1) {
        sqlite3_finalize(st);
        rc = FL_RESULT_NOENT;
        goto rollback;
    }
    sqlite3_finalize(st);
    if (log_spike(s, d->id, spike, actor, now) != FL_RESULT_OK ||
        sqlite3_exec(s->db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
        goto rollback;
    rc = FL_RESULT_OK;
    goto out;
rollback:
    sqlite3_exec(s->db, "ROLLBACK;", NULL, NULL, NULL);
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
        free(*letter);
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

size_t fl_kq_store_pending_for(fl_kq_store_t *s, const char *recipient, int64_t now,
                               fl_kq_delivery_t *out, size_t cap) {
    sqlite3_stmt *st = NULL;
    size_t n = 0;

    if (!s || !recipient || !out || cap == 0)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db,
                           "SELECT " KQ_COLUMNS " FROM kq_delivery WHERE recipient=? AND "
                           "acknowledged=0 AND (fired=1 OR expires_at>?) ORDER BY offered_at;",
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

size_t fl_kq_store_leak(fl_kq_store_t *s, int64_t now) {
    sqlite3_stmt *st = NULL;
    uint8_t ids[64][FL_KQ_DELIVERY_ID_LEN];
    size_t n = 0;

    if (!s)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db,
                           "SELECT delivery_id FROM kq_delivery WHERE fired=0 AND expires_at<=? "
                           "LIMIT 64;",
                           -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, now);
        while (n < 64 && sqlite3_step(st) == SQLITE_ROW) {
            if (sqlite3_column_bytes(st, 0) == FL_KQ_DELIVERY_ID_LEN)
                memcpy(ids[n++], sqlite3_column_blob(st, 0), FL_KQ_DELIVERY_ID_LEN);
        }
    }
    sqlite3_finalize(st);
    st = NULL;
    if (n == 0)
        goto out;
    /* Rows first, in one transaction (the delivery and its spike log); letters only once that
     * has committed, so a failure leaves a delivery that is still whole. */
    if (sqlite3_exec(s->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
        n = 0;
        goto out;
    }
    for (size_t i = 0; i < n; i++) {
        static const char *const sql[2] = {"DELETE FROM kq_spike WHERE delivery_id=?;",
                                           "DELETE FROM kq_delivery WHERE delivery_id=?;"};
        for (size_t q = 0; q < 2; q++) {
            int ok;
            if (sqlite3_prepare_v2(s->db, sql[q], -1, &st, NULL) != SQLITE_OK)
                goto rollback;
            sqlite3_bind_blob(st, 1, ids[i], FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
            ok = sqlite3_step(st) == SQLITE_DONE && (q == 0 || sqlite3_changes(s->db) == 1);
            sqlite3_finalize(st);
            st = NULL;
            if (!ok)
                goto rollback;
        }
    }
    if (sqlite3_exec(s->db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
        goto rollback;
    for (size_t i = 0; i < n; i++) {
        char path[1200];
        if (letter_path(s, ids[i], path, sizeof path) == 0)
            (void)unlink(path);
    }
    goto out;
rollback:
    (void)sqlite3_exec(s->db, "ROLLBACK;", NULL, NULL, NULL);
    n = 0;
out:
    pthread_mutex_unlock(&s->lock);
    return n;
}

size_t fl_kq_store_spike_count(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN]) {
    sqlite3_stmt *st = NULL;
    size_t n = 0;

    if (!s || !id)
        return 0;
    pthread_mutex_lock(&s->lock);
    if (sqlite3_prepare_v2(s->db, "SELECT COUNT(*) FROM kq_spike WHERE delivery_id=?;", -1, &st,
                           NULL) == SQLITE_OK) {
        sqlite3_bind_blob(st, 1, id, FL_KQ_DELIVERY_ID_LEN, SQLITE_TRANSIENT);
        if (sqlite3_step(st) == SQLITE_ROW)
            n = (size_t)sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&s->lock);
    return n;
}
