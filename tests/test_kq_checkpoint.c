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
    RUN(test_co_signed_delivery);
    RUN(test_untracked_destination_is_never_overwritten);
    RUN(test_a_damaged_history_is_refused);
    RUN(test_reasons_and_results);
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum checkpoint tests passed.\n");
    return rc;
}
