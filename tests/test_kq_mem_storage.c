/**
 * test_kq_mem_storage.c - the in-memory fl_kq_storage_t (kernel/core/vfs/kq_mem_storage.c; FL-02 stage 2).
 * The table's own rules first, then the real KeyQuorum checkpoints run on top of it with no file system: a
 * delivery is encoded, co-signed and decoded into a held directory, a failed commit installs nothing, and a
 * tracked file is signed again.
 */
#include "kq_checkpoint.h"
#include "kq_mem_storage.h"
#include "fl/driver/keyquorum.h"
#include "mem_domain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while (0)

#define MYA "Root.Flinstone.Mya"
#define STEVE "Root.Flinstone.Steve"
#define T1 "2026-11-10T09:00:00.000Z"
#define T2 "2026-11-11T10:00:00.000Z"

static const fl_kq_storage_t *st(void) { return fl_kq_storage_memory(); }

static int put(const char *path, const char *text) {
    return st()->write_atomic(st()->ctx, path, (const uint8_t *)text, strlen(text));
}

static int is(const char *path, const char *text) {
    uint8_t *d = NULL;
    size_t n = 0;
    int ok = st()->read_file(st()->ctx, path, 1u << 20, &d, &n) == 0 && n == strlen(text) && memcmp(d, text, n) == 0;

    mem_domain_free(MEM_DOMAIN_FS, d);
    return ok;
}

static int test_the_table_keeps_the_storage_contract(void) {
    uint8_t *d = NULL;
    size_t n = 0;
    fl_kq_dir_t held;

    fl_kq_storage_memory_reset();
    ASSERT(put("/m/a.txt", "hi") == 0 && st()->exists(st()->ctx, "/m/a.txt") == 1);
    ASSERT(st()->exists(st()->ctx, "/m/none") == 0);
    ASSERT(st()->read_file(st()->ctx, "/m/a.txt", 1, &d, &n) == -2 && d == NULL && n == 0); /* larger than max */
    ASSERT(st()->read_file(st()->ctx, "/m/none", 10, &d, &n) == -3 && d == NULL);            /* absent */
    ASSERT(is("/m/a.txt", "hi"));
    ASSERT(put("/m/empty", "") == 0 && st()->read_file(st()->ctx, "/m/empty", 10, &d, &n) == 0 && d && n == 0);
    mem_domain_free(MEM_DOMAIN_FS, d);

    ASSERT(st()->dir_open(st()->ctx, "/m", &held) == 0 && held.fd == -1 && strcmp(held.dir, "/m") == 0);
    ASSERT(st()->dir_exists(st()->ctx, &held, "a.txt") == 1 && st()->dir_exists(st()->ctx, &held, "b") == 0);
    ASSERT(st()->dir_read(st()->ctx, &held, "a.txt", 10, &d, &n) == 0 && n == 2);
    mem_domain_free(MEM_DOMAIN_FS, d);
    /* Names are plain: no separator, no dot names, no empty name. */
    ASSERT(st()->dir_read(st()->ctx, &held, "../x", 10, &d, &n) == -1);
    ASSERT(st()->dir_read(st()->ctx, &held, "sub/x", 10, &d, &n) == -1);
    ASSERT(st()->dir_read(st()->ctx, &held, "..", 10, &d, &n) == -1);
    ASSERT(st()->dir_read(st()->ctx, &held, "", 10, &d, &n) == -1);
    ASSERT(st()->dir_open(st()->ctx, "", &held) == -1);

    /* The newest records: 5 records of 4 bytes, keep 2 -> the last 8 bytes; absent is success with nothing; a
     * size that is not whole records is an error. */
    ASSERT(put("/m/t.bin", "0000111122223333AAAA") == 0);
    ASSERT(st()->dir_open(st()->ctx, "/m", &held) == 0);
    ASSERT(st()->dir_read_tail(st()->ctx, &held, "t.bin", 4, 2, &d, &n) == 0 && n == 8 && memcmp(d, "3333AAAA", 8) == 0);
    mem_domain_free(MEM_DOMAIN_FS, d);
    ASSERT(st()->dir_read_tail(st()->ctx, &held, "t.bin", 4, 99, &d, &n) == 0 && n == 20);
    mem_domain_free(MEM_DOMAIN_FS, d);
    ASSERT(st()->dir_read_tail(st()->ctx, &held, "nope", 4, 2, &d, &n) == 0 && d == NULL && n == 0);
    ASSERT(put("/m/odd", "12345") == 0 && st()->dir_read_tail(st()->ctx, &held, "odd", 4, 2, &d, &n) == -1);
    ASSERT(st()->dir_read_tail(st()->ctx, &held, "t.bin", 0, 2, &d, &n) == -1);
    /* A directory that already ends in '/' (the root) joins without a doubled separator: the path-based and the
     * directory-based calls reach the same file. */
    ASSERT(put("/top.txt", "root") == 0);
    ASSERT(st()->dir_open(st()->ctx, "/", &held) == 0);
    ASSERT(st()->dir_exists(st()->ctx, &held, "top.txt") == 1);
    ASSERT(st()->dir_read(st()->ctx, &held, "top.txt", 10, &d, &n) == 0 && n == 4 && memcmp(d, "root", 4) == 0);
    mem_domain_free(MEM_DOMAIN_FS, d);
    return 0;
}

static int test_a_commit_replaces_all_files_or_none(void) {
    fl_kq_dir_t held;
    fl_kq_file_out_t f[5];
    size_t before;

    fl_kq_storage_memory_reset();
    ASSERT(put("/c/one", "old1") == 0 && put("/c/two", "old2") == 0);
    ASSERT(st()->dir_open(st()->ctx, "/c", &held) == 0);
    f[0] = (fl_kq_file_out_t){"one", (const uint8_t *)"new1", 4};
    f[1] = (fl_kq_file_out_t){"two", (const uint8_t *)"new2", 4};
    f[2] = (fl_kq_file_out_t){"three", (const uint8_t *)"new3", 4};
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 3) == 0);
    ASSERT(is("/c/one", "new1") && is("/c/two", "new2") && is("/c/three", "new3"));

    before = fl_kq_storage_memory_bytes();
    /* A commit that will not fit changes nothing, the replaced files included. */
    fl_kq_storage_memory_set_limit(before + 3);
    f[0] = (fl_kq_file_out_t){"one", (const uint8_t *)"AAAA", 4};
    f[1] = (fl_kq_file_out_t){"four", (const uint8_t *)"BBBBBBBB", 8};
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 2) == -1);
    ASSERT(is("/c/one", "new1") && st()->exists(st()->ctx, "/c/four") == 0 && fl_kq_storage_memory_bytes() == before);
    fl_kq_storage_memory_set_limit(FL_KQ_MEM_BYTES_MAX);
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 2) == 0 && is("/c/one", "AAAA") && is("/c/four", "BBBBBBBB"));

    /* Bad shapes: no files, too many, the same name twice, a name that is not plain, no data. */
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 0) == -1);
    f[2] = f[3] = f[4] = f[0];
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 5) == -1);
    f[0] = (fl_kq_file_out_t){"dup", (const uint8_t *)"x", 1};
    f[1] = (fl_kq_file_out_t){"dup", (const uint8_t *)"y", 1};
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 2) == -1 && st()->exists(st()->ctx, "/c/dup") == 0);
    f[1] = (fl_kq_file_out_t){"a/b", (const uint8_t *)"y", 1};
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 2) == -1 && st()->exists(st()->ctx, "/c/dup") == 0);
    f[1] = (fl_kq_file_out_t){"ok", NULL, 1};
    ASSERT(st()->dir_commit(st()->ctx, &held, f, 2) == -1);

    /* The file table is bounded; replacing a file still works when it is full. */
    fl_kq_storage_memory_reset();
    for (unsigned i = 0; i < FL_KQ_MEM_FILES_MAX; i++) {
        char p[32];

        snprintf(p, sizeof p, "/f/%u", i);
        ASSERT(put(p, "x") == 0);
    }
    ASSERT(put("/f/extra", "x") == -1 && fl_kq_storage_memory_files() == FL_KQ_MEM_FILES_MAX);
    ASSERT(put("/f/3", "replaced") == 0 && is("/f/3", "replaced"));
    fl_kq_storage_memory_reset();
    ASSERT(fl_kq_storage_memory_files() == 0 && fl_kq_storage_memory_bytes() == 0);
    return 0;
}

/* ---- the checkpoints on this table ------------------------------------------------------------ */

typedef struct {
    const char *label;
    uint8_t sign_pub[32], box_pub[32], sign_sec[32], box_sec[32];
} acct_t;
static acct_t s_acct[2] = {{MYA, {0}, {0}, {0}, {0}}, {STEVE, {0}, {0}, {0}, {0}}};

static acct_t *acct(const char *who) {
    for (int i = 0; i < 2; i++)
        if (who && strcmp(s_acct[i].label, who) == 0)
            return &s_acct[i];
    return NULL;
}
static const char *k_resolve(const fl_kq_keydir_t *dir, const char *who) {
    const acct_t *a = acct(who);
    (void)dir;
    return a ? a->label : NULL;
}
static const unsigned char *k_sign(const fl_kq_keydir_t *dir, const char *label) {
    const acct_t *a = acct(label);
    (void)dir;
    return a ? a->sign_pub : NULL;
}
static const unsigned char *k_box(const fl_kq_keydir_t *dir, const char *label) {
    const acct_t *a = acct(label);
    (void)dir;
    return a ? a->box_pub : NULL;
}
static const fl_kq_keydir_t s_keys = {k_resolve, k_sign, k_box};

static int test_the_checkpoints_run_on_the_memory_table(void) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    fl_kq_encoded_t enc, enc2;
    fl_kq_dir_t held;
    fl_kq_buf_t lines = {0};
    uint8_t sig[64];
    int64_t last = 0, later = 0;
    size_t before;
    const fl_kq_storage_t *was;

    ASSERT(drv);
    for (int i = 0; i < 2; i++)
        ASSERT(drv->account_keygen(s_acct[i].sign_sec, s_acct[i].sign_pub) == FL_KQ_ACCOUNT_OK &&
               drv->account_box_keygen(s_acct[i].box_sec, s_acct[i].box_pub) == FL_KQ_ACCOUNT_OK);
    fl_kq_storage_memory_reset();
    was = fl_kq_cp_set_storage(fl_kq_storage_memory());

    /* Sender: the file and its history live in memory beside each other. */
    ASSERT(put("/mem/src/doc.txt", "v1\n") == 0);
    ASSERT(fl_kq_cp_encode_with(&s_keys, MYA, s_acct[0].sign_sec, STEVE, s_acct[1].box_pub, "/mem/src/doc.txt", T1, &enc) ==
           FL_KQ_CP_OK);
    ASSERT(st()->exists(st()->ctx, "/mem/src/doc.txt" FL_KQ_CONTAINER_SUFFIX) == 1);

    /* Recipient: co-signs, then decodes into a landing directory it holds. */
    ASSERT(fl_kq_cp_cosign(&enc.offer, STEVE, s_acct[1].sign_sec, sig) == FL_KQ_CP_OK);
    ASSERT(st()->dir_open(st()->ctx, "/mem/land", &held) == 0);
    ASSERT(fl_kq_cp_decode_in_with(&s_keys, MYA, s_acct[0].sign_pub, STEVE, s_acct[1].box_sec, &enc.letter, &enc.offer,
                                   sig, &held, "doc.txt") == FL_KQ_CP_OK);
    ASSERT(is("/mem/land/doc.txt", "v1\n"));
    ASSERT(st()->dir_exists(st()->ctx, &held, "doc.txt" FL_KQ_CONTAINER_SUFFIX) == 1);
    ASSERT(st()->dir_exists(st()->ctx, &held, "doc.txt" FL_KQ_SEEN_SUFFIX) == 1);

    /* A commit that cannot complete installs nothing (the payload, container and record go together). */
    ASSERT(put("/mem/src/doc2.txt", "other\n") == 0);
    ASSERT(fl_kq_cp_encode_with(&s_keys, MYA, s_acct[0].sign_sec, STEVE, s_acct[1].box_pub, "/mem/src/doc2.txt", T1, &enc2) ==
           FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_cosign(&enc2.offer, STEVE, s_acct[1].sign_sec, sig) == FL_KQ_CP_OK);
    before = fl_kq_storage_memory_bytes();
    fl_kq_storage_memory_set_limit(before + 1);
    ASSERT(fl_kq_cp_decode_in_with(&s_keys, MYA, s_acct[0].sign_pub, STEVE, s_acct[1].box_sec, &enc2.letter, &enc2.offer,
                                   sig, &held, "doc2.txt") == FL_KQ_CP_IO);
    ASSERT(st()->dir_exists(st()->ctx, &held, "doc2.txt") == 0 && st()->dir_exists(st()->ctx, &held, "doc2.txt" FL_KQ_CONTAINER_SUFFIX) == 0 &&
           st()->dir_exists(st()->ctx, &held, "doc2.txt" FL_KQ_SEEN_SUFFIX) == 0);
    fl_kq_storage_memory_set_limit(FL_KQ_MEM_BYTES_MAX);
    ASSERT(fl_kq_cp_decode_in_with(&s_keys, MYA, s_acct[0].sign_pub, STEVE, s_acct[1].box_sec, &enc2.letter, &enc2.offer,
                                   sig, &held, "doc2.txt") == FL_KQ_CP_OK); /* retries cleanly */
    ASSERT(is("/mem/land/doc2.txt", "other\n"));

    /* History, last signature and a new signature, all through the table. */
    ASSERT(fl_kq_cp_history("/mem/src/doc.txt", &lines) == FL_KQ_CP_OK && lines.len > 0);
    fl_kq_buf_free(&lines);
    ASSERT(fl_kq_cp_last_signature("/mem/src/doc.txt", &last) == FL_KQ_CP_OK && last > 0);
    ASSERT(fl_kq_cp_sign_head_with("/mem/src/doc.txt", MYA, s_acct[0].sign_sec, T2) == FL_KQ_CP_OK);
    ASSERT(fl_kq_cp_last_signature("/mem/src/doc.txt", &later) == FL_KQ_CP_OK && later > last);
    {
        char head[64];

        ASSERT(fl_kq_cp_head_author("/mem/src/doc.txt", head, sizeof head) == FL_KQ_CP_OK && strcmp(head, MYA) == 0);
    }

    fl_kq_encoded_free(&enc);
    fl_kq_encoded_free(&enc2);
    (void)fl_kq_cp_set_storage(was == fl_kq_storage_default() ? NULL : was);
    fl_kq_storage_memory_reset();
    for (int i = 0; i < 2; i++) {
        mem_domain_secure_zero(s_acct[i].sign_sec, 32);
        mem_domain_secure_zero(s_acct[i].box_sec, 32);
    }
    return 0;
}

/* The ASM-backed equality the table uses for names: equal bytes, a one-bit difference at either end, and
 * two paths of which one is a prefix of the other (the table compares lengths first, then the bytes). */
static int test_the_equality_wrapper_is_exact(void) {
    char a[8] = "/a/b/cd", b[8] = "/a/b/cd";

    if (!mem_domain_equal(a, b, 8) || !mem_domain_equal(a, b, 0))
        return 1;
    b[0] ^= 1;
    if (mem_domain_equal(a, b, 8))
        return 1;
    b[0] ^= 1;
    b[7] ^= 0x80;
    return mem_domain_equal(a, b, 8) ? 1 : 0;
}

int main(void) {
    int rc = 0;

#define RUN(t) do { if (t() != 0) { rc = 1; fprintf(stderr, "%s failed\n", #t); } else printf("%s... OK\n", #t); } while (0)
    RUN(test_the_equality_wrapper_is_exact);
    RUN(test_the_table_keeps_the_storage_contract);
    RUN(test_a_commit_replaces_all_files_or_none);
    RUN(test_the_checkpoints_run_on_the_memory_table);
    if (rc == 0)
        printf("All in-memory storage tests passed.\n");
    return rc;
}
