/**
 * test_kq_checkpoint.c - KeyQuorum security checkpoints for co-signed delivery
 * (kernel/core/vfs/kq_checkpoint.c over kernel/drivers/keyquorum/kq_driver.c;
 * contracts/drivers/contract_p4_keyquorum.h).
 *
 * Mya sends plan.txt to Steve: encode (Mya), admit (host), co-sign (Steve), decode
 * (Steve), with every refusal on the way.
 */
#include "kq_checkpoint.h"
#include "fl/mem_asm.h"
#include "user_db.h"

#include <dirent.h>
#include <limits.h>
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

#define MYA "Root.Flinstone.Mya"
#define STEVE "Root.Flinstone.Steve"
#define EVE "Root.Alternative.Eve"
#define T1 "2026-10-20T09:00:00.000Z"
#define T2 "2026-10-20T10:00:00.000Z"

static char s_dir[256];

static void path_in(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/%s", s_dir, name);
}

static int write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    return fclose(f);
}

static int read_text(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return -1;
    n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
    return 0;
}

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

typedef struct {
    fl_user_db_t db;
    unsigned char mya_sign[FL_USER_SIGNING_SECRET_LEN];
    unsigned char steve_sign[FL_USER_SIGNING_SECRET_LEN];
    unsigned char steve_box[FL_USER_SIGNING_SECRET_LEN];
    unsigned char eve_box[FL_USER_SIGNING_SECRET_LEN];
} world_t;

static int setup(world_t *w) {
    static int n;
    char name[32];
    char db_path[PATH_MAX];

    /* A fresh store per test; the lab defaults provide Root.Flinstone and Root.Alternative. */
    snprintf(name, sizeof name, "users-%d.db", ++n);
    path_in(db_path, sizeof db_path, name);
    if (fl_user_db_load(&w->db, db_path) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&w->db, "Root.Flinstone", "Mya", "pw-mya", 4000, 0, 0) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&w->db, "Root.Flinstone", "Steve", "pw-steve", 4001, 0, 0) != FL_RESULT_OK ||
        fl_user_db_add_user_under(&w->db, "Root.Alternative", "Eve", "pw-eve", 4002, 0, 0) != FL_RESULT_OK)
        return -1;
    if (fl_user_db_open_key(&w->db, "Mya", "pw-mya", w->mya_sign) != FL_RESULT_OK ||
        fl_user_db_open_key(&w->db, "Steve", "pw-steve", w->steve_sign) != FL_RESULT_OK ||
        fl_user_db_open_box(&w->db, "Steve", "pw-steve", w->steve_box) != FL_RESULT_OK ||
        fl_user_db_open_box(&w->db, "Eve", "pw-eve", w->eve_box) != FL_RESULT_OK)
        return -1;
    return 0;
}

static void teardown(world_t *w) {
    asm_mem_secure_zero(w->mya_sign, sizeof w->mya_sign);
    asm_mem_secure_zero(w->steve_sign, sizeof w->steve_sign);
    asm_mem_secure_zero(w->steve_box, sizeof w->steve_box);
    asm_mem_secure_zero(w->eve_box, sizeof w->eve_box);
}

static int contains(const fl_kq_buf_t *b, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, needle, n) == 0)
            return 1;
    return 0;
}

static int test_driver_probe(void) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    ASSERT(drv);
    ASSERT(drv->wipe == asm_mem_secure_zero && drv->equal == asm_mem_ct_equal);
    return 0;
}

/* An output that is also the input stays intact during the call and is replaced on success. */
static int test_driver_output_may_alias_its_input(void) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    world_t w;
    fl_kq_buf_t c = {0};
    uint8_t file_id[16], rev[32], head[32];
    const uint8_t text[] = "in place\n";

    ASSERT(drv && setup(&w) == 0);
    memset(file_id, 0x5A, sizeof file_id);
    ASSERT(drv->file_new(file_id, "alias.txt", MYA, &c) == KQ_FILE_OK);
    ASSERT(drv->check_in(&c, MYA, T1, text, sizeof text - 1, w.mya_sign, &c, rev) == KQ_FILE_OK);
    ASSERT(drv->verify(&c, NULL) == KQ_FILE_OK);
    ASSERT(drv->head(&c, head) == KQ_FILE_OK && memcmp(head, rev, 32) == 0);
    /* A failed call leaves the aliased buffer as it was. */
    ASSERT(drv->check_in(&c, "", T2, text, sizeof text - 1, w.mya_sign, &c, rev) != KQ_FILE_OK);
    ASSERT(c.data && drv->verify(&c, NULL) == KQ_FILE_OK);
    fl_kq_buf_free(&c);
    teardown(&w);
    return 0;
}

static int test_co_signed_delivery(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], got[256];
    uint8_t sig[64], bad[64];
    const unsigned char *mya_pub;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "plan.txt");
    path_in(dest, sizeof dest, "steve-plan.txt");
    ASSERT(write_text(src, "quarterly plan v1\n") == 0);

    /* Encode (Mya): sealed to Steve, plaintext not in the letter. */
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(!contains(&enc.letter, "quarterly plan"));
    ASSERT(strcmp(enc.file_name, "plan.txt") == 0);

    /* Latent (host): Mya's signature, not anyone else's. */
    ASSERT(fl_kq_cp_admit_offer(&w.db, MYA, &enc.offer) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_admit_offer(&w.db, STEVE, &enc.offer) == FL_KQ_CP_BAD_SIGNATURE);
    ASSERT(fl_kq_cp_admit_offer(&w.db, "Root.Nobody", &enc.offer) == FL_KQ_CP_NO_KEY);

    /* Co-sign (Steve); Mya cannot co-sign her own revision. */
    ASSERT(fl_kq_cp_cosign(&enc.offer, MYA, w.mya_sign, sig) == FL_KQ_CP_NOT_RECIPIENT);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_admit_cosign(&w.db, STEVE, &enc.offer, sig) == FL_KQ_CP_OK);
    memcpy(bad, sig, sizeof bad);
    bad[0] ^= 1u;
    ASSERT(fl_kq_cp_admit_cosign(&w.db, STEVE, &enc.offer, bad) == FL_KQ_CP_BAD_SIGNATURE);
    ASSERT(fl_kq_cp_admit_cosign(&w.db, EVE, &enc.offer, sig) == FL_KQ_CP_BAD_SIGNATURE);

    /* Decode: Eve's box key cannot open it; a bad co-signature leaves it below threshold. */
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.eve_box, &enc.letter, &enc.offer, sig,
                           dest) != FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, bad,
                           dest) == FL_KQ_CP_BELOW_THRESHOLD);
    ASSERT(access(dest, F_OK) != 0);
    /* A letter claimed to come from someone else is refused. */
    ASSERT(fl_kq_cp_decode(&w.db, STEVE, fl_user_db_signing_public(&w.db, STEVE), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_BAD_SIGNATURE);

    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(dest, got, sizeof got) == 0);
    ASSERT(strcmp(got, "quarterly plan v1\n") == 0);

    /* The same delivery again is a replay; nothing changes. */
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_REPLAYED);
    fl_kq_encoded_free(&enc);

    /* Mya edits; the second revision arrives as NEWER and replaces Steve's copy. */
    ASSERT(write_text(src, "quarterly plan v2\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T2, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(dest, got, sizeof got) == 0);
    ASSERT(strcmp(got, "quarterly plan v2\n") == 0);
    fl_kq_encoded_free(&enc);

    teardown(&w);
    return 0;
}

static int test_untracked_destination_is_never_overwritten(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], got[64];
    uint8_t sig[64];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "notes.txt");
    path_in(dest, sizeof dest, "existing.txt");
    ASSERT(write_text(src, "notes\n") == 0);
    ASSERT(write_text(dest, "mine\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_EXISTS);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "mine\n") == 0);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* A failed write leaves nothing half-installed: the first delivery retries cleanly, and an
 * update keeps the earlier copy and its history. */
static int test_a_failed_write_installs_nothing(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], got[64];
    char side[PATH_MAX + 16], seen[PATH_MAX + 16], block[PATH_MAX + 32];
    uint8_t sig[64];
    const unsigned char *mya_pub;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "atomic.txt");
    path_in(dest, sizeof dest, "steve-atomic.txt");
    snprintf(side, sizeof side, "%s%s", dest, FL_KQ_CONTAINER_SUFFIX);
    snprintf(seen, sizeof seen, "%s%s", dest, FL_KQ_SEEN_SUFFIX);
    /* A directory where the acceptance record's staging file must go makes that write fail. */
    snprintf(block, sizeof block, "%s.part", seen);
    ASSERT(write_text(src, "v1\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);

    ASSERT(mkdir(block, 0700) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_IO);
    ASSERT(access(dest, F_OK) != 0 && access(side, F_OK) != 0 && access(seen, F_OK) != 0);
    ASSERT(rmdir(block) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    fl_kq_encoded_free(&enc);

    ASSERT(write_text(src, "v2\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T2, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(mkdir(block, 0700) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_IO);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v1\n") == 0);
    ASSERT(rmdir(block) == 0);
    /* The update was not recorded, so it still arrives as newer. */
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v2\n") == 0);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

static int test_a_damaged_history_is_refused(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], side[PATH_MAX + 16];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "damaged.txt");
    snprintf(side, sizeof side, "%s%s", src, FL_KQ_CONTAINER_SUFFIX);
    ASSERT(write_text(src, "x\n") == 0);
    ASSERT(write_text(side, "not a KQTF container") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_INVALID);
    teardown(&w);
    return 0;
}

/* More accepted revisions than the record keeps: it stays bounded, and neither an old letter
 * nor the newest container naming an old revision (a rollback) is accepted. */
static int test_the_seen_record_stays_bounded(void) {
    enum { DELIVERIES = 70, KEEP = 64 };
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    world_t w;
    fl_kq_encoded_t first, enc;
    char src[PATH_MAX], dest[PATH_MAX], seen[PATH_MAX + 16], side[PATH_MAX + 16];
    char text[32], got[32], at[40];
    uint8_t sig[64];
    const unsigned char *mya_pub;
    struct stat st;

    ASSERT(drv && setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "bounded.txt");
    path_in(dest, sizeof dest, "steve-bounded.txt");
    snprintf(seen, sizeof seen, "%s%s", dest, FL_KQ_SEEN_SUFFIX);
    snprintf(side, sizeof side, "%s%s", src, FL_KQ_CONTAINER_SUFFIX);
    for (int i = 0; i < DELIVERIES; i++) {
        snprintf(text, sizeof text, "v%d\n", i);
        snprintf(at, sizeof at, "2026-10-20T09:%02d:%02d.000Z", i / 60, i % 60);
        ASSERT(write_text(src, text) == 0);
        ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                               src, at, &enc) == FL_KQ_CP_OK);
        ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
        ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer,
                               sig, dest) == FL_KQ_CP_OK);
        if (i == 0)
            first = enc;
        else
            fl_kq_encoded_free(&enc);
    }
    ASSERT(stat(seen, &st) == 0 && st.st_size == KEEP * 64);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v69\n") == 0);

    /* The first letter again: its pair has been dropped, and it is still refused. */
    ASSERT(fl_kq_cp_cosign(&first.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &first.letter, &first.offer,
                           sig, dest) == FL_KQ_CP_NOT_NEWER);

    /* A rollback: Mya's newest container, sealed for her first revision. Sealing carries only
     * that revision and its ancestors, so the newest kept pairs already refuse it; decode's
     * local_holds check also covers a crafted letter that keeps the newer revisions. */
    {
        FILE *f = fopen(side, "rb");
        fl_kq_buf_t c = {0}, letter = {0};
        kq_file_key_t key;
        kq_file_trust_t trust;
        kq_file_offer_t offer;
        uint8_t id[16];
        long n;

        ASSERT(f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0);
        c.data = (uint8_t *)malloc((size_t)n);
        ASSERT(c.data && fread(c.data, 1, (size_t)n, f) == (size_t)n);
        fclose(f);
        c.len = (size_t)n;
        key.label = MYA;
        memcpy(key.signing_public, mya_pub, 32);
        trust.keys = &key;
        trust.nkeys = 1;
        ASSERT(drv->offer(&c, first.offer.revision_id, &offer) == KQ_FILE_OK);
        ASSERT(drv->seal(&c, first.offer.revision_id, &trust, MYA, w.mya_sign,
                         fl_user_db_box_public(&w.db, MYA), STEVE,
                         fl_user_db_box_public(&w.db, STEVE), &letter, id) == KQ_FILE_OK);
        ASSERT(fl_kq_cp_cosign(&offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
        ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &letter, &offer, sig,
                               dest) == FL_KQ_CP_NOT_NEWER);
        ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v69\n") == 0);
        fl_kq_buf_free(&letter);
        fl_kq_buf_free(&c);
    }
    ASSERT(stat(seen, &st) == 0 && st.st_size == KEEP * 64);
    fl_kq_encoded_free(&first);
    teardown(&w);
    return 0;
}

static int test_only_plain_file_names_are_safe(void) {
    ASSERT(fl_kq_cp_safe_name("plan.txt"));
    ASSERT(fl_kq_cp_safe_name("Root.Flinstone.Mya"));
    ASSERT(!fl_kq_cp_safe_name(NULL));
    ASSERT(!fl_kq_cp_safe_name(""));
    ASSERT(!fl_kq_cp_safe_name("."));
    ASSERT(!fl_kq_cp_safe_name(".."));
    ASSERT(!fl_kq_cp_safe_name("../../.bashrc"));
    ASSERT(!fl_kq_cp_safe_name("/etc/passwd"));
    ASSERT(!fl_kq_cp_safe_name("dir/file"));
    ASSERT(!fl_kq_cp_safe_name("dir\\file"));
    ASSERT(!fl_kq_cp_safe_name("bell\a"));
    ASSERT(!fl_kq_cp_safe_name("esc\x1b[2Jx"));
    ASSERT(!fl_kq_cp_safe_name("del\x7f"));
    return 0;
}

static int test_reasons_and_results(void) {
    ASSERT(strcmp(fl_kq_cp_reason(FL_KQ_CP_FORKED), "file forked; merge required") == 0);
    ASSERT(fl_kq_cp_to_result(FL_KQ_CP_OK) == FL_RESULT_OK);
    ASSERT(fl_kq_cp_to_result(FL_KQ_CP_BAD_SIGNATURE) == FL_RESULT_ACCES);
    ASSERT(fl_kq_cp_to_result(FL_KQ_CP_REPLAYED) == FL_RESULT_ACCES);
    return 0;
}

int main(void) {
    char tmpl[] = "/tmp/fl_kq_cpXXXXXX";
    int rc = 0;

    (void)setenv("FL_USERS_LAB_DEFAULTS", "1", 1);
    if (!mkdtemp(tmpl))
        return 1;
    snprintf(s_dir, sizeof s_dir, "%s", tmpl);
#define RUN(t) do { \
        if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } \
        else printf("%s... OK\n", #t); \
    } while (0)
    RUN(test_driver_probe);
    RUN(test_driver_output_may_alias_its_input);
    RUN(test_co_signed_delivery);
    RUN(test_untracked_destination_is_never_overwritten);
    RUN(test_a_failed_write_installs_nothing);
    RUN(test_a_damaged_history_is_refused);
    RUN(test_the_seen_record_stays_bounded);
    RUN(test_only_plain_file_names_are_safe);
    RUN(test_reasons_and_results);
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum checkpoint tests passed.\n");
    return rc;
}
