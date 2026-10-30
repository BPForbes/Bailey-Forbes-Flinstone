/**
 * test_kq_checkpoint.c - KeyQuorum security checkpoints for co-signed delivery
 * (kernel/core/vfs/kq_checkpoint.c over kernel/drivers/keyquorum/kq_driver.c;
 * contracts/drivers/contract_p4_keyquorum.h).
 *
 * Mya sends plan.txt to Steve: encode (Mya), admit (host), co-sign (Steve), decode
 * (Steve), with every refusal on the way.
 */
#include "kq_checkpoint.h"
#include "fs_events.h"
#include "fs_facade.h"
#include "fs_provider.h"
#include "kq_storage_facade.h"
#include "fl/mem_asm.h"
#include "mem_domain.h"
#include "server_shared_fs.h"
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
/* The editor's supervisor: the lab's Flinstone account is an ancestor of Mya. */
#define SUPERVISOR "Root.Flinstone"
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
    unsigned char flinstone_sign[FL_USER_SIGNING_SECRET_LEN];
} world_t;

/* The checkpoint binary does not call drivers_init. Register the real descriptor
 * through the registry before any fl_kq_driver_get. */
static void register_kq_driver(void) {
    (void)fl_kq_driver_get_or_register();
}

static int setup(world_t *w) {
    static int n;
    char name[32];
    char db_path[PATH_MAX];

    register_kq_driver();
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
        fl_user_db_open_box(&w->db, "Eve", "pw-eve", w->eve_box) != FL_RESULT_OK ||
        fl_user_db_open_key(&w->db, "Flinstone", "flinstone", w->flinstone_sign) != FL_RESULT_OK)
        return -1;
    return 0;
}

static void teardown(world_t *w) {
    asm_mem_secure_zero(w->mya_sign, sizeof w->mya_sign);
    asm_mem_secure_zero(w->steve_sign, sizeof w->steve_sign);
    asm_mem_secure_zero(w->steve_box, sizeof w->steve_box);
    asm_mem_secure_zero(w->eve_box, sizeof w->eve_box);
    asm_mem_secure_zero(w->flinstone_sign, sizeof w->flinstone_sign);
}

static int contains(const fl_kq_buf_t *b, const char *needle) {
    size_t n = strlen(needle);
    for (size_t i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, needle, n) == 0)
            return 1;
    return 0;
}

/* The KeyQuorum driver extends the generic driver: its base is a registrable descriptor
 * whose lifecycle probe gates the KeyQuorum ops. */
static int prepare(world_t *w, const char *src, const char *text, const char *at,
                   fl_kq_encoded_t *enc, uint8_t sig[64]) {
    if (write_text(src, text) != 0 ||
        fl_kq_cp_encode(&w->db, MYA, w->mya_sign, STEVE, fl_user_db_box_public(&w->db, STEVE),
                        src, at, enc) != FL_KQ_CP_OK)
        return -1;
    if (fl_kq_cp_cosign(&enc->offer, STEVE, w->steve_sign, sig) != FL_KQ_CP_OK) {
        fl_kq_encoded_free(enc);
        return -1;
    }
    return 0;
}

/* A newer revision, made permitted: encoded, countersigned by the supervisor in the container
 * beside src, then sealed again (the revision and its history are unchanged). */
static int prepare_permitted(world_t *w, const char *src, const char *text, const char *at,
                             fl_kq_encoded_t *enc, uint8_t sig[64]) {
    fl_kq_encoded_t first;
    uint8_t rev[32];

    if (prepare(w, src, text, at, &first, sig) != 0)
        return -1;
    memcpy(rev, first.offer.revision_id, sizeof rev);
    fl_kq_encoded_free(&first);
    if (fl_kq_cp_permit(&w->db, SUPERVISOR, w->flinstone_sign, src, rev) != FL_KQ_CP_OK)
        return -1;
    if (fl_kq_cp_reseal(&w->db, MYA, w->mya_sign, STEVE, fl_user_db_box_public(&w->db, STEVE),
                        src, rev, enc) != FL_KQ_CP_OK)
        return -1;
    return 0;
}

static int test_driver_extends_the_base_driver(void) {
    const fl_driver_desc_t *base = fl_kq_driver_desc();
    const fl_kq_driver_t *drv;

    register_kq_driver();
    drv = fl_kq_driver_get();
    ASSERT(base && drv);
    ASSERT(base == fl_kq_driver_desc());
    ASSERT(base->class == FL_DRV_CLASS_SECURITY);
    ASSERT(strcmp(base->name, "keyquorum") == 0 && strcmp(base->synth_id, "keyquorum") == 0);
    ASSERT(base->ops && base->ops->probe && base->ops->probe(NULL) == FL_RESULT_OK);
    ASSERT(!base->ops->attach && !base->ops->start && !base->ops->stop && !base->ops->detach);
    /* One object: the base is the driver's first member. */
    ASSERT(fl_kq_driver_from_desc(base) == drv && &drv->base == base);
    return 0;
}

static int test_driver_probe(void) {
    const fl_kq_driver_t *drv;

    register_kq_driver();
    drv = fl_kq_driver_get();
    ASSERT(drv);
    ASSERT(drv->wipe == mem_domain_secure_zero && drv->equal == asm_mem_ct_equal);
    return 0;
}

/* An output that is also the input stays intact during the call and is replaced on success. */
static int test_driver_output_may_alias_its_input(void) {
    const fl_kq_driver_t *drv;

    register_kq_driver();
    drv = fl_kq_driver_get();
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
    ASSERT(prepare_permitted(&w, src, "quarterly plan v2\n", T2, &enc, sig) == 0);
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

    ASSERT(prepare_permitted(&w, src, "v2\n", T2, &enc, sig) == 0);
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

/* Links planted at the landing names are refused or replaced, never followed, and nothing is
 * written outside the landing directory. */
static int test_landing_names_that_are_links_are_not_followed(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], out[PATH_MAX], seen[PATH_MAX + 16], side[PATH_MAX + 16];
    char part[PATH_MAX + 32], got[64];
    uint8_t sig[64];
    const unsigned char *mya_pub;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "linked.txt");
    path_in(dest, sizeof dest, "steve-linked.txt");
    path_in(out, sizeof out, "outside-target");
    snprintf(seen, sizeof seen, "%s%s", dest, FL_KQ_SEEN_SUFFIX);
    snprintf(side, sizeof side, "%s%s", dest, FL_KQ_CONTAINER_SUFFIX);
    snprintf(part, sizeof part, "%s.part", side);
    ASSERT(write_text(src, "v1\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);

    /* A dangling link at the file's own name: not a free name, and not created through. */
    ASSERT(symlink(out, dest) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_EXISTS);
    ASSERT(access(out, F_OK) != 0);
    ASSERT(unlink(dest) == 0);

    /* A link at the acceptance record's name is refused, not read through. */
    ASSERT(write_text(out, "outside\n") == 0);
    ASSERT(symlink(out, seen) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_IO);
    ASSERT(access(dest, F_OK) != 0);
    ASSERT(read_text(out, got, sizeof got) == 0 && strcmp(got, "outside\n") == 0);
    ASSERT(unlink(seen) == 0);

    /* A link planted at a staging name is not written through, and the landing completes. */
    ASSERT(symlink(out, part) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(out, got, sizeof got) == 0 && strcmp(got, "outside\n") == 0);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v1\n") == 0);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* The quarantine gate applies to a landing, whichever way its directory is spelled. */
static int test_landing_into_the_quarantine_is_refused(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], base[PATH_MAX], dir[PATH_MAX + 64], dest[PATH_MAX + 128], ok[PATH_MAX + 64];
    uint8_t sig[64];
    const unsigned char *mya_pub;
    static const char *const forms[] = {"%s/server_shared/expired/q.txt",
                                        "%s/server_shared/./expired/q.txt",
                                        "%s/server_shared//expired/q.txt"};
    size_t i;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "gate.txt");
    path_in(base, sizeof base, "gate");
    snprintf(dir, sizeof dir, "%s/server_shared", base);
    ASSERT(mkdir(base, 0700) == 0 && mkdir(dir, 0700) == 0);
    snprintf(dir, sizeof dir, "%s/server_shared/expired", base);
    ASSERT(mkdir(dir, 0700) == 0);
    ASSERT(write_text(src, "v1\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    for (i = 0; i < sizeof forms / sizeof *forms; i++) {
        snprintf(dest, sizeof dest, forms[i], base);
        ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer,
                               sig, dest) == FL_KQ_CP_IO);
    }
    snprintf(dest, sizeof dest, "%s/q.txt", dir);
    ASSERT(access(dest, F_OK) != 0);
    snprintf(ok, sizeof ok, "%s/server_shared/q.txt", base);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           ok) == FL_KQ_CP_OK);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* Both landing paths reserve the same suffixes. */
static int test_reserved_suffixes_are_shared(void) {
    static const char *const bad[] = {"a.meta", "a.part", "a.kqtf", "a.kqseen", "a.kqbak",
                                      "a.META", "a.Part", "a.KqTf", "a.KQSEEN", "a.kqBAK"};
    size_t i;

    for (i = 0; i < sizeof bad / sizeof *bad; i++)
        ASSERT(!fl_kq_cp_safe_name(bad[i]) && fl_server_shared_name_is_reserved(bad[i]));
    ASSERT(fl_kq_cp_safe_name("a.txt") && !fl_server_shared_name_is_reserved("a.txt"));
    ASSERT(!fl_server_shared_name_is_reserved(NULL));
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
    const fl_kq_driver_t *drv;
    world_t w;
    fl_kq_encoded_t first, enc;
    char src[PATH_MAX], dest[PATH_MAX], seen[PATH_MAX + 16], side[PATH_MAX + 16];
    char text[32], got[32], at[40];
    uint8_t sig[64];
    const unsigned char *mya_pub;
    struct stat st;

    register_kq_driver();
    drv = fl_kq_driver_get();
    ASSERT(drv && setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "bounded.txt");
    path_in(dest, sizeof dest, "steve-bounded.txt");
    snprintf(seen, sizeof seen, "%s%s", dest, FL_KQ_SEEN_SUFFIX);
    snprintf(side, sizeof side, "%s%s", src, FL_KQ_CONTAINER_SUFFIX);
    for (int i = 0; i < DELIVERIES; i++) {
        snprintf(text, sizeof text, "v%d\n", i);
        snprintf(at, sizeof at, "2026-10-20T09:%02d:%02d.000Z", i / 60, i % 60);
        /* The first revision is FIRST and needs no permit; each later one is NEWER. */
        ASSERT((i == 0 ? prepare(&w, src, text, at, &enc, sig)
                       : prepare_permitted(&w, src, text, at, &enc, sig)) == 0);
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

/* Encodes text as Mya's next revision of src, co-signed by Steve; decode into dest is left to
 * the caller. */
/* The history is read from the local container alone, and it names what happened: a check-in and
 * the signature on it, then the permit on the revision; the head is the permitted revision. */
static int test_history_is_offline_and_named(void) {
    world_t w;
    fl_kq_encoded_t enc;
    fl_kq_buf_t lines = {0};
    char src[PATH_MAX];
    uint8_t sig[64], head[32];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "offline.txt");
    ASSERT(prepare(&w, src, "v1\n", T1, &enc, sig) == 0);
    fl_kq_encoded_free(&enc);

    ASSERT(fl_kq_cp_head(src, head) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_history(src, &lines) == FL_KQ_CP_OK);
    ASSERT(lines.len > 0 && lines.data[lines.len - 1] == '\n');
    ASSERT(contains(&lines, "\t2\t") && contains(&lines, "\t3\t"));
    ASSERT(strcmp(fl_kq_cp_event_name(2), "EditCheckedIn") == 0);
    ASSERT(strcmp(fl_kq_cp_event_name(3), "RevisionSigned") == 0);
    ASSERT(strcmp(fl_kq_cp_event_name(200), "?") == 0);
    ASSERT(strcmp(fl_kq_cp_outcome_name(1), "Success") == 0);
    fl_kq_buf_free(&lines);
    teardown(&w);
    return 0;
}

/* Pins the names KeyQuorum exports (kq_file_event_name / kq_file_outcome_name, through the driver)
 * to its wire codes (src/file_history/event.rs: HistoryEventType 1..=47 and HistoryOutcome 1..=4,
 * "never renumber, only append"). The literals below are an independent expected list: a renamed
 * or renumbered variant fails here, and a new variant is appended here. Flinstone keeps no table
 * of its own. */
static int test_event_and_outcome_names_pinned(void) {
    static const char *const events[] = {
        /*  1 */ "TrackingStarted",
        /*  2 */ "EditCheckedIn",
        /*  3 */ "RevisionSigned",
        /*  4 */ "CountersignatureAdded",
        /*  5 */ "PolicyDecision",
        /*  6 */ "ShareAttempted",
        /*  7 */ "ShareDelivered",
        /*  8 */ "FileExpired",
        /*  9 */ "ContentDestroyed",
        /* 10 */ "TamperDetected",
        /* 11 */ "AutoMergeAttempted",
        /* 12 */ "AutoMergeFastForward",
        /* 13 */ "AutoMergeEquivalent",
        /* 14 */ "AutoMergeClean",
        /* 15 */ "AutoMergeBlocked",
        /* 16 */ "AutoMergeRequiresHuman",
        /* 17 */ "HistoryForkDetected",
        /* 18 */ "ContentConflictDetected",
        /* 19 */ "ConflictReviewAssigned",
        /* 20 */ "ConflictReviewEscalated",
        /* 21 */ "BridgeUsed",
        /* 22 */ "ConflictUnresolved",
        /* 23 */ "HistoryImported",
        /* 24 */ "QuorumUnlockAttempted",
        /* 25 */ "PasswordUnlockAttempted",
        /* 26 */ "ExpiredAccessAttempt",
        /* 27 */ "GateLinked",
        /* 28 */ "ShareLinkCreated",
        /* 29 */ "ShareLinkRedeemed",
        /* 30 */ "ShareLinkRevoked",
        /* 31 */ "ExpiryScheduled",
        /* 32 */ "FileRenamed",
        /* 33 */ "RevisionFinalized",
        /* 34 */ "ConflictResolved",
        /* 35 */ "MergeRejected",
        /* 36 */ "RevisionCheckedOut",
        /* 37 */ "VerificationRun",
        /* 38 */ "HistoryExported",
        /* 39 */ "FileRequested",
        /* 40 */ "ChangeRequested",
        /* 41 */ "RequestAnswered",
        /* 42 */ "LetterQueued",
        /* 43 */ "LetterSent",
        /* 44 */ "LetterDropped",
        /* 45 */ "LetterRefused",
        /* 46 */ "LetterReceived",
        /* 47 */ "LetterOpened",
    };
    static const char *const outcomes[] = {"Success", "Failure", "Denied", "Info"};
    const size_t n_events = sizeof events / sizeof events[0];
    const size_t n_outcomes = sizeof outcomes / sizeof outcomes[0];
    unsigned c;

    ASSERT(n_events == 47u);
    ASSERT(n_outcomes == 4u);
    for (c = 1; c <= n_events; c++)
        ASSERT(strcmp(fl_kq_cp_event_name((uint8_t)c), events[c - 1u]) == 0);
    for (c = 1; c <= n_outcomes; c++)
        ASSERT(strcmp(fl_kq_cp_outcome_name((uint8_t)c), outcomes[c - 1u]) == 0);

    /* Every code outside the table falls back to "?" (never NULL, never an out-of-bounds read). */
    ASSERT(strcmp(fl_kq_cp_event_name(0), "?") == 0);
    for (c = (unsigned)n_events + 1u; c <= 255u; c++)
        ASSERT(strcmp(fl_kq_cp_event_name((uint8_t)c), "?") == 0);
    ASSERT(strcmp(fl_kq_cp_outcome_name(0), "?") == 0);
    for (c = (unsigned)n_outcomes + 1u; c <= 255u; c++)
        ASSERT(strcmp(fl_kq_cp_outcome_name((uint8_t)c), "?") == 0);
    return 0;
}

typedef struct {
    size_t seen;
    size_t stop_at; /* stop after this many lines; 0 never stops */
    char first[32];
    char last[32];
} walk_t;

static int walk_cb(const char *line, size_t n, void *ctx) {
    walk_t *w = ctx;
    size_t m = n < sizeof w->last - 1u ? n : sizeof w->last - 1u;

    if (w->seen == 0u) {
        memcpy(w->first, line, m);
        w->first[m] = '\0';
    }
    memcpy(w->last, line, m);
    w->last[m] = '\0';
    w->seen++;
    return w->stop_at != 0u && w->seen == w->stop_at;
}

/* The line walk: no lines, one line, a trailing fragment without a newline (skipped, not a
 * line), many lines in order, and a callback that stops the walk. */
static int test_for_each_event_walks_lines(void) {
    static const char one[] = "a\tb\n";
    static const char partial[] = "x\ny\nfrag";
    static const char many[] = "l1\nl2\n\nl4\nl5\n";
    fl_kq_buf_t b;
    walk_t w;

    memset(&w, 0, sizeof w);
    b.data = NULL;
    b.len = 0;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 0u && w.seen == 0u);
    ASSERT(fl_kq_cp_for_each_event(NULL, walk_cb, &w) == 0u);
    b.data = (uint8_t *)(void *)one;
    b.len = 0;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 0u && w.seen == 0u);

    b.len = sizeof one - 1u;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 1u);
    ASSERT(w.seen == 1u && strcmp(w.first, "a\tb") == 0);

    memset(&w, 0, sizeof w);
    b.data = (uint8_t *)(void *)partial;
    b.len = sizeof partial - 1u;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 2u);
    ASSERT(strcmp(w.first, "x") == 0 && strcmp(w.last, "y") == 0);
    b.len = 4u; /* "x\ny\n": both lines complete */
    memset(&w, 0, sizeof w);
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 2u);
    b.len = 3u; /* "x\ny" : one line, "y" is a fragment */
    memset(&w, 0, sizeof w);
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 1u && strcmp(w.last, "x") == 0);
    b.len = 1u; /* "x" alone: no line */
    memset(&w, 0, sizeof w);
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 0u && w.seen == 0u);

    memset(&w, 0, sizeof w);
    b.data = (uint8_t *)(void *)many;
    b.len = sizeof many - 1u;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 5u);
    ASSERT(strcmp(w.first, "l1") == 0 && strcmp(w.last, "l5") == 0);

    memset(&w, 0, sizeof w);
    w.stop_at = 2u;
    ASSERT(fl_kq_cp_for_each_event(&b, walk_cb, &w) == 2u);
    ASSERT(w.seen == 2u && strcmp(w.last, "l2") == 0);
    ASSERT(fl_kq_cp_for_each_event(&b, NULL, &w) == 0u);
    return 0;
}

/* A newer copy is refused until the editor's supervisor has countersigned it: Steve keeps the copy
 * he has, and the same revision goes through once the permit is in the container. */
static int test_newer_without_the_permit_is_refused(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], got[64];
    uint8_t sig[64];
    const unsigned char *mya_pub;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "permit.txt");
    path_in(dest, sizeof dest, "steve-permit.txt");

    ASSERT(prepare(&w, src, "v1\n", T1, &enc, sig) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    fl_kq_encoded_free(&enc);

    ASSERT(prepare(&w, src, "v2\n", T2, &enc, sig) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_NEEDS_EDITOR_PERMIT);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v1\n") == 0);
    fl_kq_encoded_free(&enc);

    ASSERT(prepare_permitted(&w, src, "v2\n", T2, &enc, sig) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v2\n") == 0);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* Once a file has been accepted, a newer delivery is checked against the accepted container
 * beside it. Without that exact container (missing, damaged, or another file's) the check
 * cannot be made, and the delivery is refused rather than assumed new. */
static int test_newer_needs_the_accepted_container(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], other[PATH_MAX], dest[PATH_MAX], dest2[PATH_MAX], got[64];
    char side[PATH_MAX + 16], side2[PATH_MAX + 16], kept[PATH_MAX + 32];
    uint8_t sig[64];
    const unsigned char *mya_pub;

    ASSERT(setup(&w) == 0);
    mya_pub = fl_user_db_signing_public(&w.db, MYA);
    path_in(src, sizeof src, "accepted.txt");
    path_in(other, sizeof other, "unrelated.txt");
    path_in(dest, sizeof dest, "steve-accepted.txt");
    path_in(dest2, sizeof dest2, "steve-unrelated.txt");
    snprintf(side, sizeof side, "%s%s", dest, FL_KQ_CONTAINER_SUFFIX);
    snprintf(side2, sizeof side2, "%s%s", dest2, FL_KQ_CONTAINER_SUFFIX);
    snprintf(kept, sizeof kept, "%s.kept", side);

    ASSERT(prepare(&w, src, "v1\n", T1, &enc, sig) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    fl_kq_encoded_free(&enc);
    ASSERT(prepare(&w, other, "other\n", T1, &enc, sig) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest2) == FL_KQ_CP_OK);
    fl_kq_encoded_free(&enc);

    ASSERT(prepare_permitted(&w, src, "v2\n", T2, &enc, sig) == 0);
    /* Missing. */
    ASSERT(rename(side, kept) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_IO);
    /* Damaged. */
    ASSERT(write_text(side, "not a KQTF container") == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_INVALID);
    /* A valid container, but not the one accepted for this file. */
    ASSERT(rename(side2, side) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_INVALID);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v1\n") == 0);

    /* The accepted container back in place: the same delivery is now newer. */
    ASSERT(rename(kept, side) == 0);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, mya_pub, STEVE, w.steve_box, &enc.letter, &enc.offer, sig,
                           dest) == FL_KQ_CP_OK);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "v2\n") == 0);
    fl_kq_encoded_free(&enc);
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
    ASSERT(fl_kq_cp_safe_name("part"));
    ASSERT(fl_kq_cp_safe_name("notes.partial"));
    ASSERT(!fl_kq_cp_safe_name("plan.txt.part"));
    ASSERT(!fl_kq_cp_safe_name("plan.txt.kqtf"));
    ASSERT(!fl_kq_cp_safe_name("plan.txt.kqseen"));
    ASSERT(!fl_kq_cp_safe_name("plan.txt.kqbak"));
    ASSERT(!fl_kq_cp_safe_name(".part"));
    return 0;
}


/* ---- the storage table (kq_storage.h) ------------------------------------ */

/* A table that forwards to the default and counts each call, so a test can see that every
 * checkpoint read and write goes through it. */
typedef struct {
    int read_file, write_atomic, exists, dir_open, dir_close, dir_read, dir_exists, dir_read_tail,
        dir_commit;
    int fail_exists;     /* answer "cannot tell" to exists */
    int fail_commit;     /* refuse dir_commit */
} counts_t;

static counts_t s_counts;

static int c_read_file(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len) {
    (void)ctx;
    s_counts.read_file++;
    return fl_kq_storage_default()->read_file(NULL, path, max, data, len);
}
static int c_write_atomic(void *ctx, const char *path, const uint8_t *data, size_t len) {
    (void)ctx;
    s_counts.write_atomic++;
    return fl_kq_storage_default()->write_atomic(NULL, path, data, len);
}
static int c_exists(void *ctx, const char *path) {
    (void)ctx;
    s_counts.exists++;
    if (s_counts.fail_exists)
        return -1;
    return fl_kq_storage_default()->exists(NULL, path);
}
static int c_dir_open(void *ctx, const char *dir, fl_kq_dir_t *out) {
    (void)ctx;
    s_counts.dir_open++;
    return fl_kq_storage_default()->dir_open(NULL, dir, out);
}
static void c_dir_close(void *ctx, fl_kq_dir_t *d) {
    (void)ctx;
    s_counts.dir_close++;
    fl_kq_storage_default()->dir_close(NULL, d);
}
static int c_dir_read(void *ctx, const fl_kq_dir_t *d, const char *name, size_t max,
                      uint8_t **data, size_t *len) {
    (void)ctx;
    s_counts.dir_read++;
    return fl_kq_storage_default()->dir_read(NULL, d, name, max, data, len);
}
static int c_dir_exists(void *ctx, const fl_kq_dir_t *d, const char *name) {
    (void)ctx;
    s_counts.dir_exists++;
    return fl_kq_storage_default()->dir_exists(NULL, d, name);
}
static int c_dir_read_tail(void *ctx, const fl_kq_dir_t *d, const char *name, size_t unit,
                           size_t keep, uint8_t **data, size_t *len) {
    (void)ctx;
    s_counts.dir_read_tail++;
    return fl_kq_storage_default()->dir_read_tail(NULL, d, name, unit, keep, data, len);
}
static int c_dir_commit(void *ctx, const fl_kq_dir_t *d, const fl_kq_file_out_t *files,
                        size_t n) {
    (void)ctx;
    s_counts.dir_commit++;
    if (s_counts.fail_commit)
        return -1;
    return fl_kq_storage_default()->dir_commit(NULL, d, files, n);
}

static const fl_kq_storage_t k_counting = {
    NULL,        c_read_file,  c_write_atomic,  c_exists,       c_dir_open,
    c_dir_close, c_dir_read,   c_dir_exists,    c_dir_read_tail, c_dir_commit,
};

static void use_counting(void) {
    memset(&s_counts, 0, sizeof s_counts);
    (void)fl_kq_cp_set_storage(&k_counting);
}

static void use_default(void) {
    (void)fl_kq_cp_set_storage(NULL);
}

/* Every checkpoint that touches a file does it through the installed table. */
static int test_checkpoints_use_the_storage_table(void) {
    world_t w;
    fl_kq_encoded_t enc;
    fl_kq_buf_t lines = {0};
    char src[PATH_MAX], dest[PATH_MAX], got[64];
    uint8_t sig[64], head[32];

    ASSERT(fl_kq_cp_storage() == fl_kq_storage_default());
    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "routed.txt");
    path_in(dest, sizeof dest, "routed-copy.txt");
    ASSERT(write_text(src, "routed\n") == 0);

    use_counting();
    ASSERT(fl_kq_cp_storage() == &k_counting);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    /* the file, whether a container is there, and the new container */
    ASSERT(s_counts.read_file == 1 && s_counts.exists == 1 && s_counts.write_atomic == 1);
    ASSERT(fl_kq_cp_head(src, head) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_history(src, &lines) == FL_KQ_CP_OK);
    fl_kq_buf_free(&lines);
    ASSERT(s_counts.read_file == 3);

    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_OK);
    /* one held directory, opened and closed once; the leaf and its seen record looked up;
     * the seen record read from its tail; the three files replaced together */
    ASSERT(s_counts.dir_open == 1 && s_counts.dir_close == 1);
    ASSERT(s_counts.dir_exists >= 1 && s_counts.dir_read_tail == 1);
    ASSERT(s_counts.dir_commit == 1);
    use_default();
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "routed\n") == 0);

    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* base + suffix without a truncating snprintf into a PATH_MAX buffer. */
static int with_suffix(char *out, size_t cap, const char *base, const char *suffix) {
    size_t n = strlen(base), m = strlen(suffix);

    if (n + m + 1u > cap)
        return -1;
    memcpy(out, base, n);
    memcpy(out + n, suffix, m + 1u);
    return 0;
}

/* A refused commit installs nothing and still releases the directory it held. */
static int test_a_refused_commit_installs_nothing(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX], dest[PATH_MAX], sidecar_path[PATH_MAX];
    uint8_t sig[64];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "refused.txt");
    path_in(dest, sizeof dest, "refused-copy.txt");
    ASSERT(write_text(src, "refused\n") == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);

    use_counting();
    s_counts.fail_commit = 1;
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_IO);
    ASSERT(s_counts.dir_commit == 1 && s_counts.dir_open == s_counts.dir_close);
    ASSERT(with_suffix(sidecar_path, sizeof sidecar_path, dest, FL_KQ_CONTAINER_SUFFIX) == 0);
    ASSERT(access(dest, F_OK) != 0 && access(sidecar_path, F_OK) != 0);
    ASSERT(with_suffix(sidecar_path, sizeof sidecar_path, dest, FL_KQ_SEEN_SUFFIX) == 0);
    ASSERT(access(sidecar_path, F_OK) != 0);

    /* the table allows it again and the same delivery then lands (nothing was half-installed) */
    s_counts.fail_commit = 0;
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_OK);
    use_default();
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}

/* A container that cannot be looked at is not "absent": encode fails instead of starting a new
 * history over it. */
static int test_an_unreadable_container_is_not_replaced(void) {
    world_t w;
    fl_kq_encoded_t enc;
    char src[PATH_MAX];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "unlooked.txt");
    ASSERT(write_text(src, "unlooked\n") == 0);
    use_counting();
    s_counts.fail_exists = 1;
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_IO);
    ASSERT(s_counts.write_atomic == 0);
    use_default();
    teardown(&w);
    return 0;
}

/* The decode that is given a held directory writes into that directory, never into whatever
 * the path names now, and leaves the caller's handle open. */
static int test_decode_uses_the_held_directory(void) {
    world_t w;
    fl_kq_encoded_t enc;
    fl_kq_dir_t held;
    char src[PATH_MAX], land[PATH_MAX], moved[PATH_MAX], landed[PATH_MAX], got[64];
    struct stat st;
    uint8_t sig[64];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "held.txt");
    path_in(land, sizeof land, "land-held");
    path_in(moved, sizeof moved, "land-held-moved");
    ASSERT(write_text(src, "held\n") == 0);
    ASSERT(mkdir(land, 0700) == 0);
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);

    ASSERT(fl_kq_cp_storage()->dir_open(NULL, land, &held) == 0);
    /* swap the directory for an impostor after it was held (and "checked") */
    ASSERT(rename(land, moved) == 0);
    ASSERT(mkdir(land, 0700) == 0);

    ASSERT(fl_kq_cp_decode_in(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE,
                              w.steve_box, &enc.letter, &enc.offer, sig, &held,
                              "copy.txt") == FL_KQ_CP_OK);
    ASSERT(fl_fs_join_path(landed, sizeof landed, moved, "copy.txt") == 0);
    ASSERT(read_text(landed, got, sizeof got) == 0 && strcmp(got, "held\n") == 0);
    ASSERT(fl_fs_join_path(landed, sizeof landed, land, "copy.txt") == 0);
    ASSERT(access(landed, F_OK) != 0);
    /* the caller's handle is still theirs: open, and the checkpoint did not close it */
    ASSERT(held.fd < 0 || fstat(held.fd, &st) == 0);
    fl_kq_cp_storage()->dir_close(NULL, &held);

    /* bad arguments are refused before anything is touched */
    ASSERT(fl_kq_cp_decode_in(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE,
                              w.steve_box, &enc.letter, &enc.offer, sig, NULL,
                              "copy.txt") == FL_KQ_CP_INVALID);
    held.fd = -1;
    held.dir = land;
    ASSERT(fl_kq_cp_decode_in(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE,
                              w.steve_box, &enc.letter, &enc.offer, sig, &held,
                              "../escape") == FL_KQ_CP_INVALID);
    fl_kq_encoded_free(&enc);
    teardown(&w);
    return 0;
}


/* ---- the native shell's table: the facade ----------------------------------- */

static int s_saved;

static void on_saved(const fs_event_t *ev, void *ud) {
    (void)ev;
    (void)ud;
    s_saved++;
}

static int deny_seen_write(fs_access_policy_t *pol, const char *user, const char *path) {
    (void)pol;
    (void)user;
    return strstr(path, FL_KQ_SEEN_SUFFIX) == NULL;
}

static int allow_any(fs_access_policy_t *pol, const char *user, const char *path) {
    (void)pol;
    (void)user;
    (void)path;
    return 1;
}

static const fs_access_policy_vtable_t k_deny_seen_vt = {allow_any, deny_seen_write, allow_any};

/* With the facade installed every checkpoint read and write is the facade's: the save event
 * follows each file of a delivery, and a policy that refuses one member of the decode's
 * commit stops all three before any is staged. */
static int test_checkpoints_through_the_facade(void) {
    world_t w;
    fl_kq_encoded_t enc;
    fs_provider_t *prov;
    file_manager_service_t *svc;
    fs_access_policy_t deny = {&k_deny_seen_vt, NULL};
    fl_kq_storage_t table;
    char src[PATH_MAX], dest[PATH_MAX], got[64], extra[PATH_MAX];
    uint8_t sig[64];

    ASSERT(setup(&w) == 0);
    path_in(src, sizeof src, "face.txt");
    path_in(dest, sizeof dest, "face-copy.txt");
    ASSERT(write_text(src, "through the facade\n") == 0);
    fs_events_init();
    fs_events_subscribe(FS_EV_FILE_SAVED, on_saved, NULL);
    ASSERT((prov = fs_local_provider_create()) != NULL);
    ASSERT((svc = fm_service_create(prov)) != NULL);
    ASSERT(fl_kq_storage_facade(svc, &table) == &table);
    ASSERT(fl_kq_storage_facade(NULL, &table) == NULL);
    (void)fl_kq_cp_set_storage(&table);

    /* encode: the container is written by the facade (one save event) */
    s_saved = 0;
    ASSERT(fl_kq_cp_encode(&w.db, MYA, w.mya_sign, STEVE, fl_user_db_box_public(&w.db, STEVE),
                           src, T1, &enc) == FL_KQ_CP_OK);
    ASSERT(s_saved == 1);
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, w.steve_sign, sig) == FL_KQ_CP_OK);

    /* a policy that refuses the seen record: nothing is staged, no event, no file */
    fm_service_set_policy(svc, &deny);
    s_saved = 0;
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_IO);
    ASSERT(s_saved == 0 && access(dest, F_OK) != 0);
    ASSERT(snprintf(extra, sizeof extra, "%s.part", dest) < (int)sizeof extra &&
           access(extra, F_OK) != 0);

    /* allowed: the three files land together, one event each */
    fm_service_set_policy(svc, NULL);
    ASSERT(fl_kq_cp_decode(&w.db, MYA, fl_user_db_signing_public(&w.db, MYA), STEVE, w.steve_box,
                           &enc.letter, &enc.offer, sig, dest) == FL_KQ_CP_OK);
    ASSERT(s_saved == 3);
    ASSERT(read_text(dest, got, sizeof got) == 0 && strcmp(got, "through the facade\n") == 0);

    (void)fl_kq_cp_set_storage(NULL);
    fl_kq_encoded_free(&enc);
    fm_service_destroy(svc);
    fs_provider_destroy(prov);
    fs_events_shutdown();
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
    RUN(test_driver_extends_the_base_driver);
    RUN(test_driver_probe);
    RUN(test_driver_output_may_alias_its_input);
    RUN(test_co_signed_delivery);
    RUN(test_untracked_destination_is_never_overwritten);
    RUN(test_a_failed_write_installs_nothing);
    RUN(test_landing_names_that_are_links_are_not_followed);
    RUN(test_landing_into_the_quarantine_is_refused);
    RUN(test_reserved_suffixes_are_shared);
    RUN(test_a_damaged_history_is_refused);
    RUN(test_the_seen_record_stays_bounded);
    RUN(test_newer_needs_the_accepted_container);
    RUN(test_newer_without_the_permit_is_refused);
    RUN(test_history_is_offline_and_named);
    RUN(test_event_and_outcome_names_pinned);
    RUN(test_for_each_event_walks_lines);
    RUN(test_only_plain_file_names_are_safe);
    RUN(test_checkpoints_use_the_storage_table);
    RUN(test_a_refused_commit_installs_nothing);
    RUN(test_an_unreadable_container_is_not_replaced);
    RUN(test_decode_uses_the_held_directory);
    RUN(test_checkpoints_through_the_facade);
    RUN(test_reasons_and_results);
    rmrf(s_dir);
    if (rc == 0)
        printf("All KeyQuorum checkpoint tests passed.\n");
    return rc;
}
