/* Shared test fixtures. KQ_TEST_UTIL_SQLITE and KQ_TEST_UTIL_KQ, defined before the include, add the
 * sqlite and KeyQuorum helpers for tests that link them. */
#ifndef KQ_TEST_UTIL_H
#define KQ_TEST_UTIL_H

#include "fl/hex.h"
#include "fl/random.h"

#include <dirent.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Never follows a symbolic link. 0, or -1 if anything is left. */
static inline int kq_test_rmrf(const char *path) {
    struct stat st;
    struct dirent *e;
    DIR *d;
    int rc = 0;

    if (lstat(path, &st) != 0)
        return 0;
    if (!S_ISDIR(st.st_mode))
        return unlink(path) == 0 ? 0 : -1;
    d = opendir(path);
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (snprintf(sub, sizeof sub, "%s/%s", path, e->d_name) >= (int)sizeof sub || kq_test_rmrf(sub) != 0)
            rc = -1;
    }
    closedir(d);
    return rmdir(path) == 0 ? rc : -1;
}

static inline int kq_test_write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return -1;
    ok = n == 0 || fwrite(data, 1, n, f) == n;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

static inline int kq_test_write_text(const char *path, const char *text) {
    return kq_test_write_file(path, text, strlen(text));
}

/* nbytes (at most 64) drawn bytes as lower-case hex; out holds 2 * nbytes + 1. */
static inline int kq_test_random_hex(char *out, size_t nbytes) {
    uint8_t raw[64];
    volatile uint8_t *wipe = raw;
    int rc = -1;

    out[0] = '\0';
    if (nbytes != 0 && nbytes <= sizeof raw && fl_random_bytes(raw, nbytes) == 0) {
        fl_hex_encode(raw, nbytes, out);
        rc = 0;
    }
    for (size_t i = 0; i < sizeof raw; i++)
        wipe[i] = 0;
    return rc;
}

#ifdef KQ_TEST_UTIL_SQLITE
#include <sqlite3.h>

static inline int kq_test_open_db(const char *db_path, sqlite3 **out) {
    if (sqlite3_open(db_path, out) == SQLITE_OK)
        return 0;
    sqlite3_close(*out);
    *out = NULL;
    return -1;
}

static inline int kq_test_exec_sql(const char *db_path, const char *sql) {
    sqlite3 *db = NULL;
    int rc;

    if (kq_test_open_db(db_path, &db) != 0)
        return -1;
    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_close(db);
    return rc == SQLITE_OK ? 0 : -1;
}

/* label's own pairing row; kqst_path may be NULL, the event hash is event_hash_len zero bytes. */
static inline int kq_test_pair_account(const char *db_path, const char *label, const char *kqst_path,
                                       const uint8_t device_id[16], const uint8_t signing_public[32],
                                       const uint8_t encryption_public[32], int event_hash_len, int replace) {
    static const char cols[] =
        " INTO account_keys (label,purpose,holds_share,minted_by,minted_at,generation,kqst_path,device_id,"
        "signing_public,encryption_public,ledger_seq,event_hash) VALUES (?1,'own',1,'Root',1,1,?2,?3,?4,?5,0,?6);";
    char stmt[sizeof cols + 32];
    sqlite3 *db = NULL;
    sqlite3_stmt *st = NULL;
    int ok = 0;

    if (kq_test_open_db(db_path, &db) != 0)
        return -1;
    (void)sqlite3_busy_timeout(db, 5000);
    snprintf(stmt, sizeof stmt, "%s%s", replace ? "INSERT OR REPLACE" : "INSERT", cols);
    if (sqlite3_prepare_v2(db, stmt, -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_text(st, 1, label, -1, SQLITE_TRANSIENT);
        if (kqst_path)
            sqlite3_bind_text(st, 2, kqst_path, -1, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 3, device_id, 16, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 4, signing_public, 32, SQLITE_TRANSIENT);
        sqlite3_bind_blob(st, 5, encryption_public, 32, SQLITE_TRANSIENT);
        sqlite3_bind_zeroblob(st, 6, event_hash_len);
        ok = sqlite3_step(st) == SQLITE_DONE;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return ok ? 0 : -1;
}
#endif

#ifdef KQ_TEST_UTIL_KQ
#include "fl/driver/keyquorum.h"
#include "kq_checkpoint.h"
#include "server_shared_fs.h"

static inline int kq_test_put_container(const char *path, const fl_kq_buf_t *c) {
    char kqtf[PATH_MAX];

    if (snprintf(kqtf, sizeof kqtf, "%s%s", path, FL_KQ_CONTAINER_SUFFIX) >= (int)sizeof kqtf)
        return -1;
    return fl_fs_write_file_atomic(kqtf, c->data, c->len);
}

/* Two heads from one: "a\n" at t_left and "b\n" at t_right on the head of one, merged at t_merge. */
static inline int kq_test_fork(const fl_kq_driver_t *drv, const fl_kq_buf_t *one, const char *author,
                               const uint8_t key[32], const char *t_left, const char *t_right,
                               const char *t_merge, fl_kq_buf_t *forked) {
    fl_kq_buf_t left = {0}, right = {0};
    uint8_t rev[32];
    int rc = -1;

    if (drv && drv->check_in(one, author, t_left, (const uint8_t *)"a\n", 2, key, &left, rev) == KQ_FILE_OK &&
        drv->check_in(one, author, t_right, (const uint8_t *)"b\n", 2, key, &right, rev) == KQ_FILE_OK &&
        drv->merge(&left, &right, author, t_merge, forked) == KQ_FILE_OK)
        rc = 0;
    fl_kq_buf_free(&left);
    fl_kq_buf_free(&right);
    if (rc != 0)
        fl_kq_buf_free(forked);
    return rc;
}
#endif

#endif
