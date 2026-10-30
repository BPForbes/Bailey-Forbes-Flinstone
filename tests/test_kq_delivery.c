/**
 * test_kq_delivery.c - the host's co-signed delivery store (kernel/core/vfs/kq_delivery.c):
 * integrate-and-fire at FL_KQ_HOST_THRESHOLD, fire once, leak on the server clock, and the
 * store on the host catalog's rows with its held letters (contracts/drivers/contract_p4_keyquorum.h).
 */
#include "kq_delivery.h"
#include "server_shared_db.h"
#include "server_shared_fs.h"

#include <dirent.h>
#include <sqlite3.h>
#include <limits.h>
#include "mem_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

static char s_dir[256];

static void rmrf(const char *path) {
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d) {
        unlink(path);
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char sub[PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        rmrf(sub);
    }
    closedir(d);
    rmdir(path);
}

static void make_id(uint8_t id[FL_KQ_DELIVERY_ID_LEN], uint8_t seed) {
    for (size_t i = 0; i < FL_KQ_DELIVERY_ID_LEN; i++)
        id[i] = (uint8_t)(seed + i);
}

static int test_neuron_fires_once_at_threshold(void) {
    fl_kq_delivery_t d;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];

    make_id(id, 1);
    memset(sig, 0xA5, sizeof sig);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "plan.txt", NULL,
                        1000, 60);
    ASSERT(d.potential == 1u && d.threshold == FL_KQ_HOST_THRESHOLD);
    /* Announcing is not a signature. */
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_ANNOUNCE, NULL, 1001) == FL_KQ_NEURON_REST);
    ASSERT(d.potential == 1u);
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1002) == FL_KQ_NEURON_FIRE);
    ASSERT(d.fired && d.has_cosig && d.potential == 2u);
    /* Refractory: a second co-signature never releases again. */
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1003) == FL_KQ_NEURON_REFRACTORY);
    ASSERT(d.potential == 2u);
    return 0;
}

static int test_neuron_leaks_after_expiry(void) {
    fl_kq_delivery_t d;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];

    make_id(id, 2);
    memset(sig, 0x5A, sizeof sig);
    fl_kq_delivery_init(&d, id, "a", "b", "f", NULL, 1000, 60);
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, 1060) == FL_KQ_NEURON_LEAKED);
    ASSERT(!d.fired && !d.has_cosig && d.potential == 1u);
    return 0;
}

static int test_ids_round_trip(void) {
    uint8_t id[FL_KQ_DELIVERY_ID_LEN], back[FL_KQ_DELIVERY_ID_LEN];
    char hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];

    make_id(id, 0xF0);
    fl_kq_delivery_id_hex(id, hex);
    ASSERT(strlen(hex) == 32);
    ASSERT(fl_kq_delivery_id_parse(hex, back) == 0 && memcmp(id, back, sizeof id) == 0);
    ASSERT(fl_kq_delivery_id_parse("zz", back) != 0);
    hex[3] = 'g';
    ASSERT(fl_kq_delivery_id_parse(hex, back) != 0);
    return 0;
}

/* The events recorded for a delivery, in order, joined with ',' into out. */
static int events_of(fl_kq_store_t *s, const uint8_t id[FL_KQ_DELIVERY_ID_LEN], char *out,
                     size_t cap) {
    fl_server_catalog_event_t ev[16];
    size_t n = 0;

    out[0] = '\0';
    if (fl_kq_store_events(s, id, ev, 16, &n) != FL_RESULT_OK)
        return -1;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(ev[i].detail, "kq") != 0 || ev[i].actor_member_id != 0)
            return -1; /* events name no account */
        snprintf(out + strlen(out), cap - strlen(out), "%s%s", i ? "," : "", ev[i].event);
    }
    return 0;
}

static int test_store_round_trip_and_leak(void) {
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t d, got, pending[4];
    uint8_t id[FL_KQ_DELIVERY_ID_LEN], id2[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];
    const uint8_t letter[] = "sealed bytes the host cannot open";
    uint8_t *held = NULL;
    size_t held_len = 0;
    char log[256];
    char db_path[sizeof s_dir + 32];
    struct stat sb;

    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    /* The deliveries are rows of the share root's catalog file, which is owner-only. */
    snprintf(db_path, sizeof db_path, "%s/" FL_SERVER_CATALOG_DB_NAME, s_dir);
    ASSERT(stat(db_path, &sb) == 0 && (sb.st_mode & 0777) == 0600);
    make_id(id, 10);
    make_id(id2, 20);
    memset(sig, 0x11, sizeof sig);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "plan.txt", NULL,
                        2000, 100);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_BUSY);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK);
    ASSERT(strcmp(got.recipient, "Root.Flinstone.Steve") == 0 && got.potential == 1u);
    ASSERT(strcmp(got.sender, "Root.Flinstone.Mya") == 0 && strcmp(got.file_name, "plan.txt") == 0);
    ASSERT(got.offered_at == 2000 && got.expires_at == 2100 && !got.fired && !got.expired);
    ASSERT(fl_kq_store_letter(s, id, &held, &held_len) == FL_RESULT_OK);
    ASSERT(held_len == sizeof letter && memcmp(held, letter, held_len) == 0);
    mem_domain_free(MEM_DOMAIN_FS, held);

    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", 2001, pending, 4) == 1);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Eve", 2001, pending, 4) == 0);

    /* Fire: the co-signature and the release are recorded together. */
    ASSERT(fl_kq_delivery_integrate(&got, FL_KQ_SPIKE_COSIGN, sig, 2010) == FL_KQ_NEURON_FIRE);
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_COSIGN, 2010) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK && got.fired && got.has_cosig);
    ASSERT(got.potential == 2u && memcmp(got.cosig, sig, sizeof sig) == 0);
    ASSERT(events_of(s, id, log, sizeof log) == 0 && strcmp(log, "offered,cosigned,released") == 0);
    /* Released once: a second co-signature changes nothing. */
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_COSIGN, 2011) == FL_RESULT_NOENT);
    ASSERT(events_of(s, id, log, sizeof log) == 0 && strcmp(log, "offered,cosigned,released") == 0);

    /* A second delivery that nobody co-signs expires with its letter. */
    fl_kq_delivery_init(&d, id2, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "old.txt", NULL,
                        2000, 10);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    ASSERT(fl_kq_store_leak(s, 2005) == 0);
    /* While the catalog is locked elsewhere the leak changes nothing, the letter included. */
    {
        sqlite3 *other = NULL;

        ASSERT(sqlite3_open(db_path, &other) == SQLITE_OK);
        ASSERT(sqlite3_exec(other, "BEGIN EXCLUSIVE;", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_busy_timeout(other, 0);
        ASSERT(fl_kq_store_leak(s, 2010) == 0);
        ASSERT(sqlite3_exec(other, "ROLLBACK;", NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(other);
        ASSERT(fl_kq_store_letter(s, id2, &held, &held_len) == FL_RESULT_OK);
        mem_domain_free(MEM_DOMAIN_FS, held);
        ASSERT(events_of(s, id2, log, sizeof log) == 0 && strcmp(log, "offered") == 0);
    }
    ASSERT(fl_kq_store_leak(s, 2010) == 1);
    ASSERT(fl_kq_store_letter(s, id2, &held, &held_len) == FL_RESULT_NOENT);
    /* The row and its history stay, ended, so the id is never held again. */
    ASSERT(fl_kq_store_get(s, id2, &got) == FL_RESULT_OK && got.expired && !got.fired);
    ASSERT(events_of(s, id2, log, sizeof log) == 0 && strcmp(log, "offered,expired") == 0);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_BUSY);
    ASSERT(fl_kq_store_letter(s, id2, &held, &held_len) == FL_RESULT_NOENT);
    ASSERT(fl_kq_delivery_integrate(&got, FL_KQ_SPIKE_COSIGN, sig, 2011) == FL_KQ_NEURON_LEAKED);
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_COSIGN, 2011) == FL_RESULT_NOENT);
    ASSERT(fl_kq_store_leak(s, 2011) == 0);

    /* Acknowledged: off the pending list, letter deleted, history kept. */
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK);
    got.acknowledged = 1;
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_ACK, 2020) == FL_RESULT_OK);
    ASSERT(fl_kq_store_forget_letter(s, id) == FL_RESULT_OK);
    ASSERT(fl_kq_store_letter(s, id, &held, &held_len) == FL_RESULT_NOENT);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", 2021, pending, 4) == 0);
    ASSERT(events_of(s, id, log, sizeof log) == 0 &&
           strcmp(log, "offered,cosigned,released,acknowledged") == 0);
    /* The sender's listing holds both, newest first, with the host's state for each. */
    {
        fl_kq_delivery_t sent[4];

        ASSERT(fl_kq_store_sent_by(s, "Root.Flinstone.Mya", sent, 4) == 2);
        ASSERT(strcmp(fl_kq_delivery_state(&sent[0]), "delivered") == 0 ||
               strcmp(fl_kq_delivery_state(&sent[1]), "delivered") == 0);
        ASSERT(strcmp(fl_kq_delivery_state(&sent[0]), "expired") == 0 ||
               strcmp(fl_kq_delivery_state(&sent[1]), "expired") == 0);
        ASSERT(fl_kq_store_sent_by(s, "Root.Flinstone.Steve", sent, 4) == 0);
    }
    /* Delivered never leaks and never acknowledges twice. */
    ASSERT(fl_kq_store_leak(s, 999999) == 0);
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_ACK, 2022) == FL_RESULT_NOENT);
    fl_kq_store_close(s);

    /* It reopens at the same schema version. */
    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK && got.acknowledged);
    fl_kq_store_close(s);
    return 0;
}

/* A letter released but never acknowledged is not held forever. */
static int test_released_letter_expires(void) {
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t d, got, pending[2];
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    uint8_t sig[64];
    const uint8_t letter[] = "released, never acknowledged";
    uint8_t *held = NULL;
    size_t held_len = 0;
    char log[256];
    const int64_t at = 5000;

    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    make_id(id, 40);
    memset(sig, 0x22, sizeof sig);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "late.txt", NULL,
                        at, 60);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    ASSERT(fl_kq_delivery_integrate(&d, FL_KQ_SPIKE_COSIGN, sig, at + 1) == FL_KQ_NEURON_FIRE);
    ASSERT(fl_kq_store_update(s, &d, FL_KQ_SPIKE_COSIGN, at + 1) == FL_RESULT_OK);
    /* Past the offer's own expiry it is still pending: the release clock runs from release. */
    ASSERT(fl_kq_store_leak(s, at + 120) == 0);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", at + 120, pending, 2) == 1);
    ASSERT(fl_kq_store_leak(s, at + 1 + FL_KQ_RELEASE_TTL_SEC - 1) == 0);
    ASSERT(fl_kq_store_leak(s, at + 1 + FL_KQ_RELEASE_TTL_SEC) == 1);
    ASSERT(fl_kq_store_letter(s, id, &held, &held_len) == FL_RESULT_NOENT);
    ASSERT(fl_kq_store_get(s, id, &got) == FL_RESULT_OK && got.expired && got.fired);
    ASSERT(fl_kq_store_pending_for(s, "Root.Flinstone.Steve", at + 1 + FL_KQ_RELEASE_TTL_SEC,
                                   pending, 2) == 0);
    ASSERT(events_of(s, id, log, sizeof log) == 0 &&
           strcmp(log, "offered,cosigned,released,expired") == 0);
    /* A late receipt does not revive it. */
    got.acknowledged = 1;
    ASSERT(fl_kq_store_update(s, &got, FL_KQ_SPIKE_ACK, at + 2 + FL_KQ_RELEASE_TTL_SEC) ==
           FL_RESULT_NOENT);
    fl_kq_store_close(s);
    return 0;
}

/* The share path and the store share one file but not their rows. */
static int test_share_path_cannot_reach_kq_rows(void) {
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t d;
    uint8_t id[FL_KQ_DELIVERY_ID_LEN];
    const uint8_t letter[] = "sealed";
    char db_path[sizeof s_dir + 32];
    char transfer[64], hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    fl_server_catalog_event_t ev[4];
    size_t n = 1;
    sqlite3 *share = NULL;

    ASSERT(fl_kq_store_open(s_dir, &s) == FL_RESULT_OK);
    make_id(id, 60);
    fl_kq_delivery_init(&d, id, "Root.Flinstone.Mya", "Root.Flinstone.Steve", "x.txt", NULL, 7000,
                        60);
    ASSERT(fl_kq_store_put(s, &d, letter, sizeof letter) == FL_RESULT_OK);
    fl_kq_delivery_id_hex(id, hex);
    snprintf(transfer, sizeof transfer, "kq-%s", hex);
    snprintf(db_path, sizeof db_path, "%s/" FL_SERVER_CATALOG_DB_NAME, s_dir);
    ASSERT(fl_catalog_file_open(db_path, &share) == FL_RESULT_OK);
    ASSERT(fl_catalog_file_list_events(share, 0, transfer, ev, 4, &n) == FL_RESULT_OK && n == 0);
    ASSERT(fl_catalog_file_append_event(share, 0, transfer, "revoked", 1, 7001, "") ==
           FL_RESULT_ACCES);
    /* And the store writes no history for an id that is not its row. */
    ASSERT(fl_catalog_file_append_event(share, 1, "not-a-kq-row", "released", 0, 7001, "kq") ==
           FL_RESULT_ACCES);
    sqlite3_close(share);
    fl_kq_store_close(s);
    return 0;
}

/* The schema older builds wrote to <root>/kq/kq_delivery.db, written here by hand so the test
 * does not depend on any code that still writes it. */
static const char *kLegacySchema =
    "CREATE TABLE kq_delivery (delivery_id BLOB PRIMARY KEY NOT NULL, sender TEXT NOT NULL,"
    " recipient TEXT NOT NULL, file_name TEXT NOT NULL, offer BLOB NOT NULL,"
    " potential INTEGER NOT NULL, threshold INTEGER NOT NULL, offered_at INTEGER NOT NULL,"
    " expires_at INTEGER NOT NULL, fired INTEGER NOT NULL DEFAULT 0,"
    " acknowledged INTEGER NOT NULL DEFAULT 0, cosig BLOB);"
    "CREATE TABLE kq_spike (delivery_id BLOB NOT NULL, seq INTEGER NOT NULL, kind INTEGER NOT NULL,"
    " actor TEXT NOT NULL, at INTEGER NOT NULL, PRIMARY KEY (delivery_id, seq));";

static int legacy_row(sqlite3 *db, const uint8_t id[FL_KQ_DELIVERY_ID_LEN], const char *file,
                      int64_t at, int fired, int acked) {
    sqlite3_stmt *st = NULL;
    kq_file_offer_t offer;
    uint8_t cosig[64];
    int ok;

    memset(&offer, 0x5C, sizeof offer);
    memset(cosig, 0x77, sizeof cosig);
    if (sqlite3_prepare_v2(db, "INSERT INTO kq_delivery VALUES(?1,'Root.Flinstone.Mya',"
                               "'Root.Flinstone.Steve',?2,?3,?4,2,?5,?6,?7,?8,?9);",
                           -1, &st, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_blob(st, 1, id, FL_KQ_DELIVERY_ID_LEN, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, file, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 3, &offer, (int)sizeof offer, SQLITE_STATIC);
    sqlite3_bind_int(st, 4, fired ? 2 : 1);
    sqlite3_bind_int64(st, 5, at);
    sqlite3_bind_int64(st, 6, at + 100);
    sqlite3_bind_int(st, 7, fired);
    sqlite3_bind_int(st, 8, acked);
    if (fired)
        sqlite3_bind_blob(st, 9, cosig, (int)sizeof cosig, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 9);
    ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : -1;
}

static int legacy_spike(sqlite3 *db, const uint8_t id[FL_KQ_DELIVERY_ID_LEN], int seq, int kind,
                        int64_t at) {
    sqlite3_stmt *st = NULL;
    int ok;

    if (sqlite3_prepare_v2(db, "INSERT INTO kq_spike VALUES(?1,?2,?3,'x',?4);", -1, &st, NULL) !=
        SQLITE_OK)
        return -1;
    sqlite3_bind_blob(st, 1, id, FL_KQ_DELIVERY_ID_LEN, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, seq);
    sqlite3_bind_int(st, 3, kind);
    sqlite3_bind_int64(st, 4, at);
    ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok ? 0 : -1;
}

static int write_bytes(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return -1;
    ok = fputs(text, f) >= 0;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

/* Deliveries an older build held come into the catalog once, with their history and letters. */
static int test_older_store_is_carried_over(void) {
    char root[sizeof s_dir + 32], dir[sizeof root + 8], letters[sizeof dir + 16];
    char db_path[sizeof dir + 32], path[sizeof letters + 64], hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    uint8_t held[FL_KQ_DELIVERY_ID_LEN], rel[FL_KQ_DELIVERY_ID_LEN], done[FL_KQ_DELIVERY_ID_LEN];
    uint8_t lost[FL_KQ_DELIVERY_ID_LEN];
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t got;
    sqlite3 *old = NULL;
    uint8_t *letter = NULL;
    size_t letter_len = 0;
    char log[256];

    snprintf(root, sizeof root, "%s/carry", s_dir);
    snprintf(dir, sizeof dir, "%s/kq", root);
    snprintf(letters, sizeof letters, "%s/kq_letters", dir);
    ASSERT(fl_fs_mkdir_p(letters, 0700) == 0);
    snprintf(db_path, sizeof db_path, "%s/kq_delivery.db", dir);
    make_id(held, 80);
    make_id(rel, 90);
    make_id(done, 100);
    make_id(lost, 110);
    ASSERT(sqlite3_open(db_path, &old) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, kLegacySchema, NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, "PRAGMA user_version=1;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(legacy_row(old, held, "held.txt", 1000, 0, 0) == 0);
    ASSERT(legacy_spike(old, held, 1, FL_KQ_SPIKE_OFFER, 1000) == 0);
    ASSERT(legacy_spike(old, held, 2, FL_KQ_SPIKE_ANNOUNCE, 1000) == 0);
    ASSERT(legacy_row(old, rel, "rel.txt", 1001, 1, 0) == 0);
    ASSERT(legacy_spike(old, rel, 1, FL_KQ_SPIKE_OFFER, 1001) == 0);
    ASSERT(legacy_spike(old, rel, 2, FL_KQ_SPIKE_COSIGN, 1050) == 0);
    ASSERT(legacy_spike(old, rel, 3, FL_KQ_SPIKE_RELEASE, 1050) == 0);
    ASSERT(legacy_row(old, done, "done.txt", 1002, 1, 1) == 0);
    ASSERT(legacy_row(old, lost, "lost.txt", 1003, 0, 0) == 0);
    sqlite3_close(old);
    fl_kq_delivery_id_hex(held, hex);
    snprintf(path, sizeof path, "%s/%s.kqpb", letters, hex);
    ASSERT(write_bytes(path, "held letter") == 0);
    fl_kq_delivery_id_hex(rel, hex);
    snprintf(path, sizeof path, "%s/%s.kqpb", letters, hex);
    ASSERT(write_bytes(path, "released letter") == 0);

    ASSERT(fl_kq_store_open(root, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, held, &got) == FL_RESULT_OK && !got.fired && !got.expired);
    ASSERT(strcmp(got.file_name, "held.txt") == 0 && got.expires_at == 1100);
    ASSERT(fl_kq_store_letter(s, held, &letter, &letter_len) == FL_RESULT_OK);
    ASSERT(letter_len == strlen("held letter") && memcmp(letter, "held letter", letter_len) == 0);
    mem_domain_free(MEM_DOMAIN_FS, letter);
    ASSERT(events_of(s, held, log, sizeof log) == 0 && strcmp(log, "offered") == 0);
    ASSERT(fl_kq_store_get(s, rel, &got) == FL_RESULT_OK && got.fired && got.has_cosig &&
           !got.acknowledged);
    ASSERT(events_of(s, rel, log, sizeof log) == 0 && strcmp(log, "offered,cosigned,released") == 0);
    /* The release clock runs from the logged release. */
    ASSERT(fl_kq_store_leak(s, 1050 + FL_KQ_RELEASE_TTL_SEC - 1) == 1); /* held: past its expiry */
    ASSERT(fl_kq_store_get(s, rel, &got) == FL_RESULT_OK && !got.expired);
    ASSERT(fl_kq_store_get(s, done, &got) == FL_RESULT_OK && got.acknowledged);
    /* A live delivery whose letter is missing cannot be released: it comes in ended. */
    ASSERT(fl_kq_store_get(s, lost, &got) == FL_RESULT_OK && got.expired);
    fl_kq_store_close(s);
    /* Read once: the old file is renamed and a reopen changes nothing. */
    ASSERT(access(db_path, F_OK) != 0);
    snprintf(path, sizeof path, "%s.migrated", db_path);
    ASSERT(access(path, F_OK) == 0);
    ASSERT(fl_kq_store_open(root, &s) == FL_RESULT_OK);
    ASSERT(events_of(s, rel, log, sizeof log) == 0 && strcmp(log, "offered,cosigned,released") == 0);
    fl_kq_store_close(s);

    /* A file from an unknown schema is left exactly as it was. */
    snprintf(root, sizeof root, "%s/carry2", s_dir);
    snprintf(dir, sizeof dir, "%s/kq", root);
    ASSERT(fl_fs_mkdir_p(dir, 0700) == 0);
    snprintf(db_path, sizeof db_path, "%s/kq_delivery.db", dir);
    ASSERT(sqlite3_open(db_path, &old) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, kLegacySchema, NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, "PRAGMA user_version=2;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(legacy_row(old, held, "held.txt", 1000, 0, 0) == 0);
    sqlite3_close(old);
    ASSERT(fl_kq_store_open(root, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, held, &got) == FL_RESULT_NOENT);
    fl_kq_store_close(s);
    ASSERT(access(db_path, F_OK) == 0);
    return 0;
}

/* The old build's files are read through held directories without following a link: a letter
 * that is a link, or a letters directory that is a link, is never read, so no other file can be
 * pulled into a sealed letter. The row is ended instead, as for any unreadable letter. */
static int test_older_store_does_not_follow_links(void) {
    char root[sizeof s_dir + 32], dir[sizeof root + 8], letters[sizeof dir + 16];
    char db_path[sizeof dir + 32], path[sizeof letters + 64], outside[sizeof s_dir + 32];
    char outside_letters[sizeof s_dir + 48], hex[2 * FL_KQ_DELIVERY_ID_LEN + 1];
    uint8_t one[FL_KQ_DELIVERY_ID_LEN], two[FL_KQ_DELIVERY_ID_LEN];
    fl_kq_store_t *s = NULL;
    fl_kq_delivery_t got;
    sqlite3 *old = NULL;
    uint8_t *letter = NULL;
    size_t letter_len = 0;

    /* case 1: the letter itself is a link to a file elsewhere */
    snprintf(root, sizeof root, "%s/carry-link", s_dir);
    snprintf(dir, sizeof dir, "%s/kq", root);
    snprintf(letters, sizeof letters, "%s/kq_letters", dir);
    snprintf(outside, sizeof outside, "%s/outside-secret.txt", s_dir);
    ASSERT(fl_fs_mkdir_p(letters, 0700) == 0);
    ASSERT(write_bytes(outside, "not a letter") == 0);
    snprintf(db_path, sizeof db_path, "%s/kq_delivery.db", dir);
    make_id(one, 120);
    ASSERT(sqlite3_open(db_path, &old) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, kLegacySchema, NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, "PRAGMA user_version=1;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(legacy_row(old, one, "one.txt", 1000, 0, 0) == 0);
    sqlite3_close(old);
    fl_kq_delivery_id_hex(one, hex);
    snprintf(path, sizeof path, "%s/%s.kqpb", letters, hex);
    ASSERT(symlink(outside, path) == 0);
    ASSERT(fl_kq_store_open(root, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, one, &got) == FL_RESULT_OK && got.expired);
    ASSERT(fl_kq_store_letter(s, one, &letter, &letter_len) != FL_RESULT_OK);
    mem_domain_free(MEM_DOMAIN_FS, letter);
    fl_kq_store_close(s);
    snprintf(path, sizeof path, "%s.migrated", db_path);
    ASSERT(access(path, F_OK) == 0); /* the file itself was still carried over and renamed */

    /* case 2: the letters directory is a link to a directory holding a real-looking letter */
    snprintf(root, sizeof root, "%s/carry-dirlink", s_dir);
    snprintf(dir, sizeof dir, "%s/kq", root);
    snprintf(letters, sizeof letters, "%s/kq_letters", dir);
    snprintf(outside_letters, sizeof outside_letters, "%s/elsewhere-letters", s_dir);
    ASSERT(fl_fs_mkdir_p(dir, 0700) == 0);
    ASSERT(fl_fs_mkdir_p(outside_letters, 0700) == 0);
    snprintf(db_path, sizeof db_path, "%s/kq_delivery.db", dir);
    make_id(two, 130);
    ASSERT(sqlite3_open(db_path, &old) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, kLegacySchema, NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(sqlite3_exec(old, "PRAGMA user_version=1;", NULL, NULL, NULL) == SQLITE_OK);
    ASSERT(legacy_row(old, two, "two.txt", 1000, 0, 0) == 0);
    sqlite3_close(old);
    fl_kq_delivery_id_hex(two, hex);
    snprintf(path, sizeof path, "%s/%s.kqpb", outside_letters, hex);
    ASSERT(write_bytes(path, "a letter elsewhere") == 0);
    ASSERT(symlink(outside_letters, letters) == 0);
    ASSERT(fl_kq_store_open(root, &s) == FL_RESULT_OK);
    ASSERT(fl_kq_store_get(s, two, &got) == FL_RESULT_OK && got.expired);
    ASSERT(fl_kq_store_letter(s, two, &letter, &letter_len) != FL_RESULT_OK);
    mem_domain_free(MEM_DOMAIN_FS, letter);
    fl_kq_store_close(s);
    return 0;
}

/* The share root may not exist yet when the host first holds a letter. */
static int test_store_creates_missing_parents(void) {
    fl_kq_store_t *s = NULL;
    char nested[sizeof s_dir + 32];
    char letters[sizeof nested + 16];

    snprintf(nested, sizeof nested, "%s/server_shared/kq", s_dir);
    ASSERT(fl_kq_store_open(nested, &s) == FL_RESULT_OK);
    fl_kq_store_close(s);
    snprintf(letters, sizeof letters, "%s/kq_letters", nested);
    ASSERT(access(letters, F_OK) == 0);

    /* A directory that cannot be created is an error, not a store opened half-way. */
    {
        char blocker[sizeof s_dir + 16];
        char under[sizeof blocker + 16];
        FILE *f;

        snprintf(blocker, sizeof blocker, "%s/plainfile", s_dir);
        f = fopen(blocker, "w");
        ASSERT(f);
        fclose(f);
        snprintf(under, sizeof under, "%s/kq", blocker);
        s = NULL;
        ASSERT(fl_kq_store_open(under, &s) == FL_RESULT_ERR && s == NULL);
    }
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_deliveryXXXXXX";
    int rc = 0;

    if (!mkdtemp(tmpl))
        return 1;
    snprintf(s_dir, sizeof s_dir, "%s", tmpl);
#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_neuron_fires_once_at_threshold);
    RUN(test_neuron_leaks_after_expiry);
    RUN(test_ids_round_trip);
    RUN(test_store_round_trip_and_leak);
    RUN(test_released_letter_expires);
    RUN(test_share_path_cannot_reach_kq_rows);
    RUN(test_older_store_is_carried_over);
    RUN(test_older_store_does_not_follow_links);
    RUN(test_store_creates_missing_parents);
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum delivery store tests passed.\n");
    return rc;
}
