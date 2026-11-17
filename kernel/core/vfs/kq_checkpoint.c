#include "kq_checkpoint.h"
#include "kq_storage.h"
#include "contract_p2_heritage.h"
#include "server_shared_fs.h"
#include "fl/hex.h"
#include "fl/path.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "net_endian.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FL_FREESTANDING
/* The image: no placement layer or shared folder; the in-memory table is the default, and every
 * checkpoint runs from the one shell context, so the container lock is not needed. */
#include "kq_mem_storage.h"
#define KQ_LOCK() ((void)0)
#define KQ_UNLOCK() ((void)0)
#else
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define KQ_LOCK() pthread_mutex_lock(&s_container_mu)
#define KQ_UNLOCK() pthread_mutex_unlock(&s_container_mu)
#endif

/* Largest file a co-signed delivery carries: half the letter cap, leaving room for the
 * container's ancestors and proofs. */
#define KQ_CONTENT_MAX (FL_KQ_LETTER_MAX / 2u)
/* Most labels a trust table holds (authors, signers, the owner). The image holds at most
 * FL_FS_MAX_USERS (8) accounts, and its kernel stack is 24 KiB: 8 entries hold every signer it knows. */
#ifdef FL_FREESTANDING
#define KQ_TRUST_MAX 8u
#else
#define KQ_TRUST_MAX 32u
#endif
/* Most (revision, root) pairs remembered per file. */
/* Accepted (revision, history root) pairs kept in <file>.kqseen (newest KQ_SEEN_KEEP only).
 * Dropping old pairs never lets an old revision back in: decode refuses it separately. */
#define KQ_SEEN_KEEP 64u

const char *fl_kq_cp_reason(fl_kq_cp_result_t r) {
    switch (r) {
    case FL_KQ_CP_OK:
        return "ok";
    case FL_KQ_CP_NO_DRIVER:
        return "KeyQuorum driver unavailable (library ABI mismatch)";
    case FL_KQ_CP_NO_KEY:
        return "an account has no KeyQuorum keys yet (sign in once to create them)";
    case FL_KQ_CP_BAD_SIGNATURE:
        return "signature does not verify";
    case FL_KQ_CP_NOT_RECIPIENT:
        return "not addressed to this account";
    case FL_KQ_CP_UNTRUSTED:
        return "revision is not trusted under the file's policy";
    case FL_KQ_CP_FORKED:
        return "file forked; merge required";
    case FL_KQ_CP_BELOW_THRESHOLD:
        return "delivery holds fewer signatures than it needs";
    case FL_KQ_CP_REPLAYED:
        return "this revision was already received (replay)";
    case FL_KQ_CP_NOT_NEWER:
        return "not newer than the copy already held";
    case FL_KQ_CP_EXISTS:
        return "an untracked file is already at the destination";
    case FL_KQ_CP_TOO_LARGE:
        return "file too large for a co-signed delivery";
    case FL_KQ_CP_IO:
        return "file could not be read or written";
    case FL_KQ_CP_NEEDS_EDITOR_PERMIT:
        return "a newer copy needs the editor's supervisor to countersign it first";
    case FL_KQ_CP_INVALID:
    default:
        return "malformed delivery";
    }
}

int fl_kq_cp_safe_name(const char *name) {
    return fl_fs_name_is_plain(name) && !fl_server_shared_name_is_reserved(name);
}

fl_result_t fl_kq_cp_to_result(fl_kq_cp_result_t r) {
    switch (r) {
    case FL_KQ_CP_OK:
        return FL_RESULT_OK;
    case FL_KQ_CP_NO_DRIVER:
        return FL_RESULT_NOSYS;
    case FL_KQ_CP_NO_KEY:
        return FL_RESULT_NOENT;
    case FL_KQ_CP_EXISTS:
        return FL_RESULT_BUSY;
    case FL_KQ_CP_TOO_LARGE:
        return FL_RESULT_NOMEM;
    case FL_KQ_CP_IO:
        return FL_RESULT_ERR;
    case FL_KQ_CP_INVALID:
        return FL_RESULT_INVAL;
    default:
        return FL_RESULT_ACCES;
    }
}

void fl_kq_encoded_free(fl_kq_encoded_t *enc) {
    if (!enc)
        return;
    fl_kq_buf_free(&enc->letter);
}

/* ---- local files -------------------------------------------------------- */

/* Write through a sibling and rename, so a reader never sees half a file. */
#ifdef FL_FREESTANDING
#define KQ_PATH_MAX FL_KQ_MEM_PATH_MAX /* the image's stack is 24 KiB; its paths are the table's */
#else
#define KQ_PATH_MAX FL_FS_PATH_MAX
#endif

#ifndef FL_FREESTANDING

/* The default storage table; the checkpoints never open a path themselves. */
static int def_read_file(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len) {
    (void)ctx;
    return fl_fs_read_file(path, max, data, len);
}

static int def_write_atomic(void *ctx, const char *path, const uint8_t *data, size_t len) {
    (void)ctx;
    return fl_fs_write_file_atomic(path, data, len);
}

/* A permission error is "cannot tell", never "absent": an unreadable container is not replaced. */
static int def_exists(void *ctx, const char *path) {
    (void)ctx;
    if (access(path, F_OK) == 0)
        return 1;
    return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
}

/* Refuses a directory that cannot be held when the contained calls exist; else path calls stand in. */
static int def_dir_open(void *ctx, const char *dir, fl_kq_dir_t *out) {
    (void)ctx;
    out->dir = dir;
    out->fd = -1;
    if (!fl_fs_contained_available())
        return 0;
    out->fd = fl_fs_dir_open(dir);
    return out->fd >= 0 ? 0 : -1;
}

static void def_dir_close(void *ctx, fl_kq_dir_t *d) {
    (void)ctx;
    if (d->fd >= 0)
        close(d->fd);
    d->fd = -1;
}

static int def_dir_read(void *ctx, const fl_kq_dir_t *d, const char *name, size_t max,
                        uint8_t **data, size_t *len) {
    (void)ctx;
    return fl_fs_read_file_at(d->fd, d->dir, name, max, data, len);
}

static int def_dir_exists(void *ctx, const fl_kq_dir_t *d, const char *name) {
    (void)ctx;
    return fl_fs_exists_at(d->fd, d->dir, name);
}

static int def_dir_read_tail(void *ctx, const fl_kq_dir_t *d, const char *name, size_t unit,
                             size_t keep, uint8_t **data, size_t *len) {
    (void)ctx;
    return fl_fs_read_tail_at(d->fd, d->dir, name, unit, keep, data, len);
}

static int def_dir_commit(void *ctx, const fl_kq_dir_t *d, const fl_kq_file_out_t *files,
                          size_t n) {
    fl_fs_file_out_t out[FL_FS_COMMIT_FILES_MAX];
    size_t i;

    (void)ctx;
    if (!files || n == 0 || n > FL_FS_COMMIT_FILES_MAX)
        return -1;
    for (i = 0; i < n; i++) {
        out[i].name = files[i].name;
        out[i].data = files[i].data;
        out[i].len = files[i].len;
    }
    return fl_fs_commit_files(d->fd, d->dir, out, n);
}

static const fl_kq_storage_t s_default_storage = {
    NULL,          def_read_file,     def_write_atomic,   def_exists,     def_dir_open,
    def_dir_close, def_dir_read,      def_dir_exists,     def_dir_read_tail, def_dir_commit,
};

const fl_kq_storage_t *fl_kq_storage_default(void) {
    return &s_default_storage;
}

/* The share's quarantine gate applies to a landing as to the share's own. */
static int quarantined(const char *a, const char *b) {
    return fl_server_shared_path_is_expired_quarantine(a) || fl_server_shared_path_is_expired_quarantine(b);
}
#else
const fl_kq_storage_t *fl_kq_storage_default(void) {
    return fl_kq_storage_memory();
}

static int quarantined(const char *a, const char *b) {
    (void)a, (void)b;
    return 0; /* the image has no shared folder */
}
#endif

static const fl_kq_storage_t *s_storage = NULL;

const fl_kq_storage_t *fl_kq_cp_storage(void) {
    return s_storage ? s_storage : fl_kq_storage_default();
}

const fl_kq_storage_t *fl_kq_cp_set_storage(const fl_kq_storage_t *storage) {
    const fl_kq_storage_t *was = fl_kq_cp_storage();

    s_storage = storage;
    return was;
}

/* A landing directory held for one checkpoint; owned means close it at the end. */
typedef struct {
    const fl_kq_storage_t *st;
    fl_kq_dir_t            dir;
    int                    owned;
} kq_land_t;

static int land_open(kq_land_t *l, const char *dir) {
    l->st = fl_kq_cp_storage();
    l->dir.fd = -1;
    l->dir.dir = dir;
    l->owned = 0;
    if (l->st->dir_open(l->st->ctx, dir, &l->dir) != 0)
        return -1;
    l->owned = 1;
    return 0;
}

static void land_close(kq_land_t *l) {
    if (l->owned && l->st)
        l->st->dir_close(l->st->ctx, &l->dir);
    l->owned = 0;
}

static int land_split(const char *dest, char *dir, size_t dcap, const char **leaf) {
    const char *slash = strrchr(dest, '/');
    size_t n;

    if (!slash) {
        *leaf = dest;
        return snprintf(dir, dcap, ".") < (int)dcap && dest[0] ? 0 : -1;
    }
    n = (size_t)(slash - dest);
    *leaf = slash + 1;
    if (!(*leaf)[0] || n + 1u > dcap)
        return -1;
    if (n == 0u) { /* "/name": the directory is the root */
        dir[0] = '/';
        dir[1] = '\0';
        return 0;
    }
    memcpy(dir, dest, n);
    dir[n] = '\0';
    return 0;
}

static int land_read(const kq_land_t *l, const char *name, uint8_t **d, size_t *n) {
    return l->st->dir_read(l->st->ctx, &l->dir, name, FL_KQ_LETTER_MAX, d, n);
}

/* 1 present (a link counts), 0 absent, -1 cannot tell. */
static int land_exists(const kq_land_t *l, const char *name) {
    return l->st->dir_exists(l->st->ctx, &l->dir, name);
}

#ifndef FL_FREESTANDING
/* Serializes every container read-modify-write, so a later write never drops an earlier revision. */
static pthread_mutex_t s_container_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static int sidecar(const char *path, const char *suffix, char *out, size_t cap) {
    return snprintf(out, cap, "%s%s", path, suffix) < (int)cap ? 0 : -1;
}

/* IO when the file cannot be read (absent included); TOO_LARGE past max. */
static fl_kq_cp_result_t read_bounded(const char *path, size_t max, fl_kq_buf_t *b) {
    int rc = fl_kq_cp_storage()->read_file(fl_kq_cp_storage()->ctx, path, max, &b->data, &b->len);

    return rc == 0 ? FL_KQ_CP_OK : rc == -2 ? FL_KQ_CP_TOO_LARGE : FL_KQ_CP_IO;
}

static fl_kq_cp_result_t load_container(const char *path, char *container_path, size_t cap,
                                        fl_kq_buf_t *c) {
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, cap) != 0)
        return FL_KQ_CP_INVALID;
    return read_bounded(container_path, FL_KQ_LETTER_MAX, c);
}

/* "<64 hex of ref>.kqer": the name a refusal report is kept under. */
static void report_name(const uint8_t ref[32], char out[65 + sizeof FL_KQ_REPORT_SUFFIX]) {
    fl_hex_encode(ref, 32, out);
    memcpy(out + 64, FL_KQ_REPORT_SUFFIX, sizeof FL_KQ_REPORT_SUFFIX);
}

/* The one write of a grown container; nothing is written when TOO_LARGE. */
static fl_kq_cp_result_t store_container(const char *container_path, const fl_kq_buf_t *next) {
    const fl_kq_storage_t *store = fl_kq_cp_storage();

    if (next->len > FL_KQ_LETTER_MAX)
        return FL_KQ_CP_TOO_LARGE;
    return store->write_atomic(store->ctx, container_path, next->data, next->len) == 0 ? FL_KQ_CP_OK : FL_KQ_CP_IO;
}

/* ---- trust table --------------------------------------------------------- */

typedef struct {
    kq_file_key_t keys[KQ_TRUST_MAX];
    char          labels[KQ_TRUST_MAX][FL_HERITAGE_KEY_ID_MAX];
    size_t        n;
    fl_kq_buf_t   signers;
} trust_table_t;

/* Signing keys for the labels the container names; unknown labels are left out. extra_label/extra_key add one vouched key. */
static void trust_build(const fl_kq_driver_t *drv, const fl_kq_keydir_t *keys,
                        const fl_kq_buf_t *c, const char *extra_label, const uint8_t *extra_key,
                        trust_table_t *t, kq_file_trust_t *out) {
    size_t off = 0;

    memset(t, 0, sizeof *t);
    if (drv->signers(c, &t->signers) == KQ_FILE_OK) {
        while (off < t->signers.len && t->n < KQ_TRUST_MAX) {
            const char *label = (const char *)t->signers.data + off;
            size_t len = strnlen(label, t->signers.len - off);
            const unsigned char *pub = NULL;

            if (off + len >= t->signers.len)
                break;
            if (extra_label && extra_key && strcmp(label, extra_label) == 0)
                pub = extra_key;
            else
                pub = keys->signing_public(keys, label);
            if (pub && len < FL_HERITAGE_KEY_ID_MAX) {
                memcpy(t->labels[t->n], label, len + 1);
                t->keys[t->n].label = t->labels[t->n];
                memcpy(t->keys[t->n].signing_public, pub, 32);
                t->n++;
            }
            off += len + 1;
        }
    }
    fl_kq_buf_free(&t->signers);
    out->keys = t->keys;
    out->nkeys = t->n;
}

/* ---- checkpoints -------------------------------------------------------- */

/* Reads path, starts its history when may_start (else refuses one that has none: IO) or loads it,
 * and checks the content in as a revision signed by author. The caller stores next. */
static fl_kq_cp_result_t checkin_core(const fl_kq_driver_t *drv, const char *path, const char *author,
                                      const uint8_t secret[32], const char *created_at_utc, int may_start,
                                      char *container_path, size_t cap, fl_kq_buf_t *next, uint8_t rev[32]) {
    const fl_kq_storage_t *store = fl_kq_cp_storage();
    fl_kq_buf_t content = {0}, container = {0};
    fl_kq_cp_result_t rc = FL_KQ_CP_OK;
    int have_container, st;

    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, cap) != 0)
        return FL_KQ_CP_INVALID;
    st = store->read_file(store->ctx, path, KQ_CONTENT_MAX, &content.data, &content.len);
    if (st == -2)
        return FL_KQ_CP_TOO_LARGE;
    if (st != 0)
        return FL_KQ_CP_IO;
    /* A container that cannot be looked at is an error, never a reason to start a new history. */
    have_container = store->exists(store->ctx, container_path);
    if (have_container < 0 || (have_container == 0 && !may_start)) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    if (have_container == 0) {
        uint8_t file_id[16];

        if (fl_random_bytes(file_id, sizeof file_id) != 0 ||
            drv->file_new(file_id, fl_fs_basename(path), author, &container) != KQ_FILE_OK) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
    } else if (store->read_file(store->ctx, container_path, FL_KQ_LETTER_MAX, &container.data,
                                &container.len) != 0 ||
               drv->verify(&container, NULL) != KQ_FILE_OK) {
        /* A damaged history is refused, never silently replaced by a new one. */
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    st = drv->check_in(&container, author, created_at_utc, content.data, content.len, secret, next, rev);
    if (st != KQ_FILE_OK)
        rc = st == KQ_FILE_FORKED ? FL_KQ_CP_FORKED : FL_KQ_CP_INVALID;
done:
    fl_kq_buf_free(&content);
    fl_kq_buf_free(&container);
    return rc;
}

static fl_kq_cp_result_t fl_kq_cp_encode_with_unlocked(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const char *created_at_utc, fl_kq_encoded_t *out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *sender_box;
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t next = {0};
    trust_table_t table;
    kq_file_trust_t trust;
    uint8_t rev[32];
    fl_kq_cp_result_t rc;
    int st;

    if (!out || !keys || !sender_key || !sender_signing_secret || !recipient_key ||
        !recipient_box_public || !path || !created_at_utc)
        return FL_KQ_CP_INVALID;
    memset(out, 0, sizeof *out);
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    sender_box = keys->box_public(keys, sender_key);
    if (!sender_box || !keys->signing_public(keys, sender_key))
        return FL_KQ_CP_NO_KEY;
    rc = checkin_core(drv, path, sender_key, sender_signing_secret, created_at_utc, 1, container_path,
                      sizeof container_path, &next, rev);
    if (rc != FL_KQ_CP_OK)
        goto done;
    if (drv->offer(&next, rev, &out->offer) != KQ_FILE_OK) {
        rc = FL_KQ_CP_BAD_SIGNATURE;
        goto done;
    }
    trust_build(drv, keys, &next, NULL, NULL, &table, &trust);
    st = drv->seal(&next, rev, &trust, sender_key, sender_signing_secret, sender_box,
                   recipient_key, recipient_box_public, &out->letter, out->delivery_id);
    if (st == KQ_FILE_UNTRUSTED) {
        rc = FL_KQ_CP_UNTRUSTED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    /* The new revision is kept beside the file only once it is sealed. */
    rc = store_container(container_path, &next);
    if (rc != FL_KQ_CP_OK)
        goto done;
    snprintf(out->file_name, sizeof out->file_name, "%s", fl_fs_basename(path));

done:
    if (rc != FL_KQ_CP_OK)
        fl_kq_encoded_free(out);
    fl_kq_buf_free(&next);
    return rc;
}

/* A first signed revision for path, kept beside it: EXISTS when it already has a history. */
static fl_kq_cp_result_t fl_kq_cp_track_with_unlocked(const fl_kq_keydir_t *keys, const char *path,
                                                      const char *author_key, const uint8_t signing_secret[32],
                                                      const char *created_at_utc, uint8_t rev_out[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const fl_kq_storage_t *store = fl_kq_cp_storage();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t next = {0};
    uint8_t rev[32];
    fl_kq_cp_result_t rc;

    if (!keys || !path || !author_key || !signing_secret || !created_at_utc)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    if (!keys->signing_public(keys, author_key))
        return FL_KQ_CP_NO_KEY;
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;
    switch (store->exists(store->ctx, container_path)) {
    case 0:
        break;
    case 1:
        return FL_KQ_CP_EXISTS;
    default:
        return FL_KQ_CP_IO;
    }
    rc = checkin_core(drv, path, author_key, signing_secret, created_at_utc, 1, container_path,
                      sizeof container_path, &next, rev);
    if (rc == FL_KQ_CP_OK)
        rc = store_container(container_path, &next);
    if (rc == FL_KQ_CP_OK && rev_out)
        memcpy(rev_out, rev, 32);
    fl_kq_buf_free(&next);
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_track_with(const fl_kq_keydir_t *keys, const char *path, const char *author_key,
                                      const uint8_t signing_secret[32], const char *created_at_utc,
                                      uint8_t rev_out[32]) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = fl_kq_cp_track_with_unlocked(keys, path, author_key, signing_secret, created_at_utc, rev_out);
    KQ_UNLOCK();
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_head_trusted_with(const fl_kq_keydir_t *keys, const char *path, uint8_t rev[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0};
    trust_table_t table;
    kq_file_trust_t trust;
    uint8_t head[32];
    fl_kq_cp_result_t rc;
    int st;

    if (!keys || !path || !rev)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    rc = load_container(path, container_path, sizeof container_path, &c);
    if (rc != FL_KQ_CP_OK)
        return rc;
    if (drv->verify(&c, NULL) != KQ_FILE_OK) {
        fl_kq_buf_free(&c);
        return FL_KQ_CP_INVALID;
    }
    st = drv->head(&c, head);
    if (st != KQ_FILE_OK) {
        fl_kq_buf_free(&c);
        return st == KQ_FILE_FORKED ? FL_KQ_CP_FORKED : FL_KQ_CP_INVALID;
    }
    /* The head as the one candidate: select_read returns it only when the policy trusts it. */
    trust_build(drv, keys, &c, NULL, NULL, &table, &trust);
    st = drv->select_read(&c, &trust, (const uint8_t(*)[32])head, 1, rev);
    fl_kq_buf_free(&c);
    if (st == KQ_FILE_OK && drv->equal(rev, head, 32))
        return FL_KQ_CP_OK;
    return st == KQ_FILE_FORKED ? FL_KQ_CP_FORKED : FL_KQ_CP_UNTRUSTED;
}

fl_kq_cp_result_t fl_kq_cp_encode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const char *created_at_utc, fl_kq_encoded_t *out) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = fl_kq_cp_encode_with_unlocked(keys, sender_key, sender_signing_secret, recipient_key, recipient_box_public, path, created_at_utc, out);
    KQ_UNLOCK();
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_admit_offer_with(const fl_kq_keydir_t *keys, const char *author_key,
                                            const kq_file_offer_t *offer) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *pub;

    if (!keys || !author_key || !offer)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    pub = keys->signing_public(keys, author_key);
    if (!pub)
        return FL_KQ_CP_NO_KEY;
    return drv->offer_verify(offer, author_key, pub) == KQ_FILE_OK ? FL_KQ_CP_OK
                                                                    : FL_KQ_CP_BAD_SIGNATURE;
}

fl_kq_cp_result_t fl_kq_cp_admit_cosign_with(const fl_kq_keydir_t *keys,
                                             const char *recipient_key,
                                             const kq_file_offer_t *offer, const uint8_t sig[64]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *pub;

    if (!keys || !recipient_key || !offer || !sig)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    pub = keys->signing_public(keys, recipient_key);
    if (!pub)
        return FL_KQ_CP_NO_KEY;
    return drv->cosign_verify(offer, recipient_key, pub, sig) == KQ_FILE_OK
               ? FL_KQ_CP_OK
               : FL_KQ_CP_BAD_SIGNATURE;
}

fl_kq_cp_result_t fl_kq_cp_cosign(const kq_file_offer_t *offer, const char *me_key,
                                  const uint8_t signing_secret[32], uint8_t sig[64]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();

    if (!offer || !me_key || !signing_secret || !sig)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    return drv->cosign(offer, me_key, signing_secret, sig) == KQ_FILE_OK ? FL_KQ_CP_OK
                                                                         : FL_KQ_CP_NOT_RECIPIENT;
}

/* Loads the newest KQ_SEEN_KEEP pairs, read from the tail of a longer file. Damaged is -1. */
static int load_seen(const kq_land_t *l, const char *name, uint8_t (**seen)[64], size_t *n) {
    uint8_t *data = NULL;
    size_t len = 0;

    *seen = NULL;
    *n = 0;
    if (l->st->dir_read_tail(l->st->ctx, &l->dir, name, 64u, KQ_SEEN_KEEP, &data, &len) != 0)
        return -1;
    *seen = (uint8_t(*)[64])data;
    *n = len / 64u;
    return 0;
}

/* Whether the accepted container beside the file already holds rev: 1 held, 0 not, -1 unreadable, -2 damaged or not the accepted copy. Fails closed. */
static int local_holds(const fl_kq_driver_t *drv, const kq_land_t *l, const char *container_name,
                       const uint8_t newest_root[32], const uint8_t rev[32]) {
    fl_kq_buf_t local = {0};
    fl_kq_buf_t held = {0};
    uint8_t root[32];
    int rc;

    if (land_read(l, container_name, &local.data, &local.len) != 0)
        return -1;
    if (drv->verify(&local, root) != KQ_FILE_OK || !drv->equal(root, newest_root, sizeof root)) {
        fl_kq_buf_free(&local);
        return -2;
    }
    rc = drv->payload(&local, rev, &held);
    fl_kq_buf_free(&held);
    fl_kq_buf_free(&local);
    if (rc == KQ_FILE_OK)
        return 1;
    return rc == KQ_FILE_NO_REVISION ? 0 : -2;
}

/* The decode steps; every read and write goes through the caller's held landp directory. */
static fl_kq_cp_result_t decode_core(const fl_kq_keydir_t *keys, const char *sender_key,
                                     const uint8_t sender_signing_public[32],
                                     const char *me_key, const uint8_t box_secret[32],
                                     const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                     const uint8_t my_sig[64], kq_land_t *landp,
                                     const char *leaf) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_name[KQ_PATH_MAX];
    char seen_name[KQ_PATH_MAX];
    fl_kq_buf_t opened = {0};
    fl_kq_buf_t cosigned = {0};
    fl_kq_buf_t payload = {0};
    uint8_t (*seen)[64] = NULL;
    size_t nseen = 0;
    trust_table_t table;
    kq_file_trust_t trust;
    uint8_t rev[32];
    uint8_t root[32];
    uint8_t pair[64];
    uint32_t have = 0;
    uint32_t need = 0;
    fl_kq_cp_result_t rc = FL_KQ_CP_OK;
    int st;

    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    if (sidecar(leaf, FL_KQ_CONTAINER_SUFFIX, container_name, sizeof container_name) != 0 ||
        sidecar(leaf, FL_KQ_SEEN_SUFFIX, seen_name, sizeof seen_name) != 0)
        return FL_KQ_CP_INVALID;
    if (land_exists(landp, leaf) == 1 && land_exists(landp, seen_name) != 1) {
        rc = FL_KQ_CP_EXISTS;
        goto done;
    }

    st = drv->open(letter, box_secret, sender_key, me_key, sender_signing_public, &opened, rev,
                   root);
    if (st != KQ_FILE_OK) {
        rc = st == KQ_FILE_UNTRUSTED ? FL_KQ_CP_BAD_SIGNATURE : FL_KQ_CP_INVALID;
        goto done;
    }
    if (!drv->equal(rev, offer->revision_id, sizeof rev)) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    if (drv->attach(&opened, offer, me_key, my_sig, &cosigned) != KQ_FILE_OK) {
        rc = FL_KQ_CP_BAD_SIGNATURE;
        goto done;
    }
    trust_build(drv, keys, &cosigned, sender_key, sender_signing_public, &table, &trust);
    st = drv->readiness(&cosigned, rev, me_key, &trust, &have, &need);
    if (st == KQ_FILE_FORKED) {
        rc = FL_KQ_CP_FORKED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_BELOW_THRESHOLD;
        goto done;
    }

    if (load_seen(landp, seen_name, &seen, &nseen) != 0) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    st = drv->freshness((const uint8_t(*)[64])seen, nseen, &cosigned, rev, root);
    if (st == KQ_FILE_REPLAYED) {
        rc = FL_KQ_CP_REPLAYED;
        goto done;
    }
    if (st != KQ_FILE_FIRST && st != KQ_FILE_NEWER) {
        rc = FL_KQ_CP_NOT_NEWER;
        goto done;
    }
    /* Checked against every accepted pair: a local container holding rev is never newer. */
    if (st == KQ_FILE_NEWER) {
        int held = nseen ? local_holds(drv, landp, container_name, seen[nseen - 1] + 32, rev) : -2;

        if (held != 0) {
            rc = held > 0 ? FL_KQ_CP_NOT_NEWER : held == -1 ? FL_KQ_CP_IO : FL_KQ_CP_INVALID;
            goto done;
        }
        /* A newer copy goes out only when the supervisor countersigned it (permit in the revision). */
        if (drv->permits_newer(&cosigned, rev, &trust) != KQ_FILE_OK) {
            rc = FL_KQ_CP_NEEDS_EDITOR_PERMIT;
            goto done;
        }
    }

    if (drv->payload(&cosigned, rev, &payload) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    /* Content, history and the accepted record land together or not at all. */
    memcpy(pair, rev, 32);
    memcpy(pair + 32, root, 32);
    {
        size_t keep = nseen < KQ_SEEN_KEEP ? nseen : KQ_SEEN_KEEP - 1u;
        size_t len = (keep + 1u) * 64u;
        uint8_t *all = (uint8_t *)mem_domain_alloc(MEM_DOMAIN_FS, len);
        fl_kq_file_out_t out[3];

        if (!all) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
        if (keep)
            memcpy(all, seen + (nseen - keep), keep * 64u);
        memcpy(all + keep * 64u, pair, 64u);
        out[0] = (fl_kq_file_out_t){leaf, payload.data, payload.len};
        out[1] = (fl_kq_file_out_t){container_name, cosigned.data, cosigned.len};
        out[2] = (fl_kq_file_out_t){seen_name, all, len};
        if (landp->st->dir_commit(landp->st->ctx, &landp->dir, out, 3) != 0)
            rc = FL_KQ_CP_IO;
        mem_domain_free(MEM_DOMAIN_FS, all);
    }

done:
    mem_domain_free(MEM_DOMAIN_FS, seen);
    fl_kq_buf_free(&opened);
    fl_kq_buf_free(&cosigned);
    fl_kq_buf_free(&payload);
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_decode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_public[32],
                                       const char *me_key, const uint8_t box_secret[32],
                                       const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                       const uint8_t my_sig[64], const char *dest_path) {
    char land_dir[KQ_PATH_MAX];
    const char *leaf = NULL;
    kq_land_t land;
    fl_kq_cp_result_t rc;

    if (!keys || !sender_key || !sender_signing_public || !me_key || !box_secret || !letter ||
        !offer || !my_sig || !dest_path)
        return FL_KQ_CP_INVALID;
    if (!fl_kq_driver_get())
        return FL_KQ_CP_NO_DRIVER;
    if (land_split(dest_path, land_dir, sizeof land_dir, &leaf) != 0)
        return FL_KQ_CP_INVALID;
    if (quarantined(land_dir, dest_path))
        return FL_KQ_CP_IO;
    if (land_open(&land, land_dir) != 0)
        return FL_KQ_CP_IO;
    rc = decode_core(keys, sender_key, sender_signing_public, me_key, box_secret, letter, offer,
                     my_sig, &land, leaf);
    land_close(&land);
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_decode_in_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                          const uint8_t sender_signing_public[32],
                                          const char *me_key, const uint8_t box_secret[32],
                                          const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                          const uint8_t my_sig[64], const fl_kq_dir_t *dir,
                                          const char *leaf) {
    char dest[KQ_PATH_MAX];
    kq_land_t land;

    if (!keys || !sender_key || !sender_signing_public || !me_key || !box_secret || !letter ||
        !offer || !my_sig || !dir || !dir->dir || !leaf || !fl_fs_name_is_plain(leaf))
        return FL_KQ_CP_INVALID;
    if (!fl_kq_driver_get())
        return FL_KQ_CP_NO_DRIVER;
    if (fl_fs_join_path(dest, sizeof dest, dir->dir, leaf) != 0)
        return FL_KQ_CP_INVALID;
    if (quarantined(dir->dir, dest))
        return FL_KQ_CP_IO;
    /* The caller's handle: used, never closed here, never opened again by path. */
    land.st = fl_kq_cp_storage();
    land.dir = *dir;
    land.owned = 0;
    return decode_core(keys, sender_key, sender_signing_public, me_key, box_secret, letter, offer,
                       my_sig, &land, leaf);
}

/* ---- history and the editor's permit (the local container) ------------- */

/* Display names of the history's wire codes; never parsed back. An unknown code prints as "?". */
const char *fl_kq_cp_event_name(uint8_t code) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const char *name = drv && drv->event_name ? drv->event_name(code) : NULL;

    return name ? name : "?";
}

const char *fl_kq_cp_outcome_name(uint8_t code) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const char *name = drv && drv->outcome_name ? drv->outcome_name(code) : NULL;

    return name ? name : "?";
}

/* One history line as text; a malformed line is copied as it is. */
size_t fl_kq_cp_format_event(const char *line, size_t n, char *out, size_t cap) {
    char text[512];
    char *field[7];
    size_t len = n < sizeof text - 1u ? n : sizeof text - 1u;
    size_t k = 1;
    int written;

    if (!out || cap == 0u)
        return 0u;
    memcpy(text, line, len);
    text[len] = '\0';
    field[0] = text;
    for (size_t i = 0; i < len && k < 7u; i++) {
        if (text[i] == '\t') {
            text[i] = '\0';
            field[k++] = text + i + 1;
        }
    }
    if (k != 7u)
        written = snprintf(out, cap, "%s", text);
    else
        written = snprintf(out, cap, "%4s  %s  %-22s %-8s by %-24s rev %-12.12s %s", field[0],
                           field[1],
                           fl_kq_cp_event_name((uint8_t)strtoul(field[2], NULL, 10)),
                           fl_kq_cp_outcome_name((uint8_t)strtoul(field[3], NULL, 10)),
                           field[4][0] ? field[4] : "UNKNOWN", field[5], field[6]);
    return written < 0 ? 0u : (size_t)written < cap ? (size_t)written : cap - 1u;
}

size_t fl_kq_cp_for_each_event(const fl_kq_buf_t *lines, fl_kq_cp_event_fn fn, void *ctx) {
    size_t start = 0;
    size_t count = 0;

    if (!lines || !lines->data || !fn)
        return 0u;
    for (size_t i = 0; i < lines->len; i++) {
        if (lines->data[i] != '\n')
            continue;
        count++;
        if (fn((const char *)lines->data + start, i - start, ctx))
            break;
        start = i + 1u;
    }
    return count;
}

fl_kq_cp_result_t fl_kq_cp_history(const char *path, fl_kq_buf_t *lines) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0};
    fl_kq_cp_result_t loaded;
    int rc;

    if (!lines || !path)
        return FL_KQ_CP_INVALID;
    lines->data = NULL;
    lines->len = 0;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    loaded = load_container(path, container_path, sizeof container_path, &c);
    if (loaded != FL_KQ_CP_OK)
        return loaded;
    rc = drv->events(&c, lines);
    fl_kq_buf_free(&c);
    return rc == KQ_FILE_OK ? FL_KQ_CP_OK : FL_KQ_CP_INVALID;
}

/* The head beside path: its revision id, or its author's label (empty for a fork). */
static fl_kq_cp_result_t head_of(const char *path, uint8_t *rev, char *author, size_t cap) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0};
    fl_kq_cp_result_t out;
    int rc;

    if (author)
        author[0] = '\0';
    if (!drv || (author && !drv->head_author))
        return FL_KQ_CP_NO_DRIVER;
    out = load_container(path, container_path, sizeof container_path, &c);
    if (out != FL_KQ_CP_OK)
        return out;
    rc = author ? drv->head_author(&c, author, cap) : drv->head(&c, rev);
    fl_kq_buf_free(&c);
    if (rc == KQ_FILE_FORKED)
        out = FL_KQ_CP_FORKED;
    else if (rc != KQ_FILE_OK)
        out = FL_KQ_CP_INVALID;
    if (author && out != FL_KQ_CP_OK)
        author[0] = '\0';
    return out;
}

fl_kq_cp_result_t fl_kq_cp_head(const char *path, uint8_t rev[32]) {
    return path && rev ? head_of(path, rev, NULL, 0) : FL_KQ_CP_INVALID;
}

fl_kq_cp_result_t fl_kq_cp_head_author(const char *path, char *out, size_t cap) {
    return path && out && cap ? head_of(path, NULL, out, cap) : FL_KQ_CP_INVALID;
}

/* Sealed letter for rev already in the container beside path. Nothing is checked or written. */
fl_kq_cp_result_t fl_kq_cp_reseal_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const uint8_t rev[32], fl_kq_encoded_t *out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *sender_box;
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t container = {0};
    trust_table_t table;
    kq_file_trust_t trust;
    fl_kq_cp_result_t rc = FL_KQ_CP_OK;
    int st;

    if (!out || !keys || !sender_key || !sender_signing_secret || !recipient_key ||
        !recipient_box_public || !path || !rev)
        return FL_KQ_CP_INVALID;
    memset(out, 0, sizeof *out);
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    sender_box = keys->box_public(keys, sender_key);
    if (!sender_box || !keys->signing_public(keys, sender_key))
        return FL_KQ_CP_NO_KEY;
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;
    if (fl_kq_cp_storage()->read_file(fl_kq_cp_storage()->ctx, container_path, FL_KQ_LETTER_MAX,
                                      &container.data, &container.len) != 0)
        return FL_KQ_CP_IO;
    if (drv->verify(&container, NULL) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    if (drv->offer(&container, rev, &out->offer) != KQ_FILE_OK) {
        rc = FL_KQ_CP_BAD_SIGNATURE;
        goto done;
    }
    trust_build(drv, keys, &container, NULL, NULL, &table, &trust);
    st = drv->seal(&container, rev, &trust, sender_key, sender_signing_secret, sender_box,
                   recipient_key, recipient_box_public, &out->letter, out->delivery_id);
    if (st == KQ_FILE_UNTRUSTED) {
        rc = FL_KQ_CP_UNTRUSTED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    snprintf(out->file_name, sizeof out->file_name, "%s", fl_fs_basename(path));

done:
    if (rc != FL_KQ_CP_OK)
        fl_kq_encoded_free(out);
    fl_kq_buf_free(&container);
    return rc;
}

static fl_kq_cp_result_t fl_kq_cp_permit_with_unlocked(const fl_kq_keydir_t *keys, const char *supervisor_key,
                                       const uint8_t supervisor_secret[32], const char *path,
                                       const uint8_t rev[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0};
    fl_kq_buf_t next = {0};
    trust_table_t table;
    kq_file_trust_t trust;
    int rc;
    fl_kq_cp_result_t out = FL_KQ_CP_OK;

    if (!drv || !keys || !supervisor_key || !supervisor_secret || !path || !rev)
        return drv ? FL_KQ_CP_INVALID : FL_KQ_CP_NO_DRIVER;
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;
    if (fl_kq_cp_storage()->read_file(fl_kq_cp_storage()->ctx, container_path, FL_KQ_LETTER_MAX,
                                      &c.data, &c.len) != 0)
        return FL_KQ_CP_IO;
    /* The author's signature must verify before the supervisor adds to it. */
    trust_build(drv, keys, &c, NULL, NULL, &table, &trust);
    rc = drv->countersign(&c, rev, supervisor_key, supervisor_secret, &trust, &next);
    if (rc != KQ_FILE_OK) {
        out = FL_KQ_CP_INVALID;
        goto done;
    }
    out = store_container(container_path, &next);

done:
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&c);
    return out;
}

fl_kq_cp_result_t fl_kq_cp_permit_with(const fl_kq_keydir_t *keys, const char *supervisor_key,
                                       const uint8_t supervisor_secret[32], const char *path,
                                       const uint8_t rev[32]) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = fl_kq_cp_permit_with_unlocked(keys, supervisor_key, supervisor_secret, path, rev);
    KQ_UNLOCK();
    return rc;
}


/* ---- refusal reports (KQER, keyquorum_error.h) --------------------------- */

void fl_kq_cp_offer_encode(const kq_file_offer_t *o, uint8_t out[FL_KQ_OFFER_WIRE_LEN]) {
    uint8_t *p = out;

    memcpy(p, o->file_id, 16);
    p += 16;
    memcpy(p, o->revision_id, 32);
    p += 32;
    memcpy(p, o->content_commitment, 32);
    p += 32;
    memcpy(p, o->author_identity, 16);
    p += 16;
    memcpy(p, o->author_signature, 64);
    p += 64;
    fl_net_put_u64_be(p, o->topology_generation);
    p += 8;
    memcpy(p, o->policy_hash, 32);
}

void fl_kq_cp_offer_decode(const uint8_t in[FL_KQ_OFFER_WIRE_LEN], kq_file_offer_t *o) {
    const uint8_t *p = in;

    memcpy(o->file_id, p, 16);
    p += 16;
    memcpy(o->revision_id, p, 32);
    p += 32;
    memcpy(o->content_commitment, p, 32);
    p += 32;
    memcpy(o->author_identity, p, 16);
    p += 16;
    memcpy(o->author_signature, p, 64);
    p += 64;
    o->topology_generation = fl_net_get_u64_be(p);
    p += 8;
    memcpy(o->policy_hash, p, 32);
}

int fl_kq_cp_report_path(const char *dir, const uint8_t ref[32], char *out, size_t cap) {
    char hex[65];

    if (!dir || !dir[0] || !ref || !out)
        return -1;
    fl_hex_encode(ref, 32, hex);
    return snprintf(out, cap, "%s/%s%s", dir, hex, FL_KQ_REPORT_SUFFIX) < (int)cap ? 0 : -1;
}

static fl_kq_cp_result_t report_write_in_unlocked(uint16_t code, uint8_t class_code, uint8_t step,
                                                  const uint8_t ref[32], const char *actor,
                                                  const char *occurred_at, const uint8_t secret[32],
                                                  const fl_kq_dir_t *dir) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const fl_kq_storage_t *st = fl_kq_cp_storage();
    char name[65 + sizeof FL_KQ_REPORT_SUFFIX];
    uint8_t ledger[FL_KQ_REPORT_CAP * 32u];
    size_t held = 0, i;
    int known = 0, e;
    fl_kq_buf_t kqer = {0}, old = {0};
    fl_kq_file_out_t files[2];
    fl_kq_cp_result_t out = FL_KQ_CP_OK;

    if (!ref || !actor || !occurred_at || !secret || !dir || !dir->dir)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    report_name(ref, name);
    /* Every step inside the held directory; an existing report is kept and not counted again. */
    e = st->dir_exists(st->ctx, dir, name);
    if (e != 0)
        return e == 1 ? FL_KQ_CP_EXISTS : FL_KQ_CP_IO;
    e = st->dir_exists(st->ctx, dir, FL_KQ_REPORT_LEDGER);
    if (e < 0)
        return FL_KQ_CP_IO;
    if (e == 1) {
        if (st->dir_read(st->ctx, dir, FL_KQ_REPORT_LEDGER, FL_KQ_REPORT_CAP * 32u, &old.data,
                         &old.len) != 0 ||
            old.len % 32u != 0) {
            fl_kq_buf_free(&old);
            return FL_KQ_CP_IO; /* a ledger that cannot be read is never taken as empty */
        }
        memcpy(ledger, old.data, old.len);
        held = old.len;
        fl_kq_buf_free(&old);
    }
    for (i = 0; i < held; i += 32u)
        if (memcmp(ledger + i, ref, 32) == 0)
            known = 1;
    if (!known && held >= FL_KQ_REPORT_CAP * 32u)
        return FL_KQ_CP_TOO_LARGE;
    if (drv->error_sign(code, class_code, step, ref, actor, occurred_at, NULL, NULL, secret,
                        &kqer) != KQ_FILE_OK)
        return FL_KQ_CP_INVALID;
    if (!known) {
        memcpy(ledger + held, ref, 32);
        held += 32u;
    }
    files[0].name = name;
    files[0].data = kqer.data;
    files[0].len = kqer.len;
    files[1].name = FL_KQ_REPORT_LEDGER;
    files[1].data = ledger;
    files[1].len = held;
    if (st->dir_commit(st->ctx, dir, files, 2) != 0)
        out = FL_KQ_CP_IO;
    fl_kq_buf_free(&kqer);
    return out;
}

/* Read, extend and commit the ledger under one lock so two reports never drop a reference. */
fl_kq_cp_result_t fl_kq_cp_report_write_in(uint16_t code, uint8_t class_code, uint8_t step,
                                           const uint8_t ref[32], const char *actor,
                                           const char *occurred_at, const uint8_t secret[32],
                                           const fl_kq_dir_t *dir) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = report_write_in_unlocked(code, class_code, step, ref, actor, occurred_at, secret, dir);
    KQ_UNLOCK();
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_report_read_in(const fl_kq_dir_t *dir, const uint8_t ref[32],
                                          const uint8_t reporter_public[32], fl_kq_buf_t *out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const fl_kq_storage_t *st = fl_kq_cp_storage();
    char name[65 + sizeof FL_KQ_REPORT_SUFFIX];
    fl_kq_buf_t scratch = {0}, next = {0};
    uint8_t id[16];
    fl_kq_cp_result_t rc = FL_KQ_CP_BAD_SIGNATURE;

    if (!out)
        return FL_KQ_CP_INVALID;
    out->data = NULL;
    out->len = 0;
    if (!dir || !dir->dir || !ref || !reporter_public)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    report_name(ref, name);
    if (st->dir_read(st->ctx, dir, name, FL_KQ_REPORT_MAX, &out->data, &out->len) != 0) {
        out->data = NULL;
        out->len = 0;
        return FL_KQ_CP_IO;
    }
    mem_domain_copy(id, ref, sizeof id);
    if (drv->file_new(id, "report-check", FL_HERITAGE_ROOT_KEY, &scratch) == KQ_FILE_OK &&
        drv->error_inject(&scratch, out, reporter_public, ref, &next) == KQ_FILE_OK)
        rc = FL_KQ_CP_OK;
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&scratch);
    if (rc != FL_KQ_CP_OK) {
        fl_kq_buf_free(out);
        out->data = NULL;
        out->len = 0;
    }
    return rc;
}

static fl_kq_cp_result_t fl_kq_cp_inject_report_bytes_unlocked(const uint8_t reporter_public[32], const uint8_t *kqer_bytes,
                                               size_t kqer_len, const uint8_t ref[32], const char *path) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0}, next = {0};
    fl_kq_buf_t kqer;
    fl_kq_cp_result_t out;

    if (!reporter_public || !kqer_bytes || kqer_len == 0 || kqer_len > FL_KQ_REPORT_MAX || !ref || !path)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    kqer.data = (uint8_t *)kqer_bytes; /* read only here; the driver does not keep it */
    kqer.len = kqer_len;
    out = load_container(path, container_path, sizeof container_path, &c);
    if (out != FL_KQ_CP_OK)
        goto done;
    /* Signature checked under the reporter's key; the report must name the held artifact. */
    if (drv->error_inject(&c, &kqer, reporter_public, ref, &next) != KQ_FILE_OK) {
        out = FL_KQ_CP_BAD_SIGNATURE;
        goto done;
    }
    out = store_container(container_path, &next);
done:
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&c);
    return out;
}

fl_kq_cp_result_t fl_kq_cp_inject_report_bytes(const uint8_t reporter_public[32], const uint8_t *kqer_bytes,
                                               size_t kqer_len, const uint8_t ref[32], const char *path) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = fl_kq_cp_inject_report_bytes_unlocked(reporter_public, kqer_bytes, kqer_len, ref, path);
    KQ_UNLOCK();
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_inject_report_with(const fl_kq_keydir_t *keys, const char *reporter_key,
                                              const char *report_path, const uint8_t ref[32],
                                              const char *path) {
    const unsigned char *reporter_public;
    fl_kq_buf_t kqer = {0};
    fl_kq_cp_result_t out;

    if (!keys || !reporter_key || !report_path || !ref || !path)
        return FL_KQ_CP_INVALID;
    if (!fl_kq_driver_get())
        return FL_KQ_CP_NO_DRIVER;
    reporter_public = keys->signing_public(keys, reporter_key);
    if (!reporter_public)
        return FL_KQ_CP_NO_KEY;
    out = read_bounded(report_path, FL_KQ_REPORT_MAX, &kqer);
    if (out != FL_KQ_CP_OK)
        return out;
    out = fl_kq_cp_inject_report_bytes(reporter_public, kqer.data, kqer.len, ref, path);
    fl_kq_buf_free(&kqer);
    return out;
}

/* ---- periodic signing -------------------------------------------------------------------- */

/* KeyQuorum history codes (src/file_history/event.rs): RevisionSigned 3, CountersignatureAdded 4;
 * outcome Success 1. Wire format: append-only on KeyQuorum's side. */
#define KQ_EV_REVISION_SIGNED 3
#define KQ_EV_COUNTERSIGNED 4
#define KQ_OUTCOME_SUCCESS 1

static int num_field(const char *s, size_t n, int64_t *out) {
    int64_t v = 0;

    if (n == 0 || n > 18)
        return -1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

/* "2026-11-10T09:00:00Z" (or with .000) as epoch seconds. */
static int utc_to_sec(const char *s, size_t n, int64_t *out) {
    int64_t y, mo, d, h, mi, se, era, yoe, doy, doe;

    if (n < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[n - 1] != 'Z')
        return -1;
    if (num_field(s, 4, &y) || num_field(s + 5, 2, &mo) || num_field(s + 8, 2, &d) ||
        num_field(s + 11, 2, &h) || num_field(s + 14, 2, &mi) || num_field(s + 17, 2, &se))
        return -1;
    if (n > 20) { /* fraction: '.' then digits, nothing else */
        if (s[19] != '.' || n < 22)
            return -1;
        for (size_t i = 20; i + 1 < n; i++)
            if (s[i] < '0' || s[i] > '9')
                return -1;
    } else if (s[19] != 'Z') {
        return -1;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60 || y < 1970)
        return -1;
    y -= mo <= 2; /* days from civil (proleptic Gregorian), shifting the year to start in March */
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    *out = ((era * 146097 + doe - 719468) * 24 + h) * 3600 + mi * 60 + se;
    return 0;
}

typedef struct {
    int64_t newest;
} last_sig_t;

/* A history line: sequence TAB time TAB type TAB outcome TAB actor ... */
static int last_sig_line(const char *line, size_t n, void *ctx) {
    last_sig_t *ls = (last_sig_t *)ctx;
    const char *f[4];
    size_t len[4], start = 0;
    int k = 0;
    int64_t type, outcome, t;

    for (size_t i = 0; i <= n && k < 4; i++) {
        if (i == n || line[i] == '\t') {
            f[k] = line + start;
            len[k] = i - start;
            k++;
            start = i + 1;
        }
    }
    if (k < 4 || num_field(f[2], len[2], &type) || num_field(f[3], len[3], &outcome))
        return 0;
    if ((type == KQ_EV_REVISION_SIGNED || type == KQ_EV_COUNTERSIGNED) && outcome == KQ_OUTCOME_SUCCESS &&
        utc_to_sec(f[1], len[1], &t) == 0 && t > ls->newest)
        ls->newest = t;
    return 0;
}

fl_kq_cp_result_t fl_kq_cp_last_signature(const char *path, int64_t *sec_out) {
    fl_kq_buf_t lines = {0};
    last_sig_t ls = {0};
    fl_kq_cp_result_t rc;

    if (!path || !sec_out)
        return FL_KQ_CP_INVALID;
    *sec_out = 0;
    rc = fl_kq_cp_history(path, &lines);
    if (rc != FL_KQ_CP_OK)
        return rc;
    (void)fl_kq_cp_for_each_event(&lines, last_sig_line, &ls);
    fl_kq_buf_free(&lines);
    *sec_out = ls.newest;
    return FL_KQ_CP_OK;
}

static fl_kq_cp_result_t fl_kq_cp_sign_head_with_unlocked(const char *path, const char *author_key,
                                          const uint8_t signing_secret[32], const char *created_at_utc) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[KQ_PATH_MAX], head[FL_HERITAGE_KEY_ID_MAX];
    fl_kq_buf_t content = {0}, container = {0}, next = {0};
    uint8_t rev[32];
    fl_kq_cp_result_t rc;
    int st;

    if (!path || !author_key || !signing_secret || !created_at_utc)
        return FL_KQ_CP_INVALID;
    if (!drv || !drv->head_author)
        return FL_KQ_CP_NO_DRIVER;
    rc = read_bounded(path, KQ_CONTENT_MAX, &content);
    if (rc != FL_KQ_CP_OK)
        return rc;
    rc = load_container(path, container_path, sizeof container_path, &container);
    if (rc != FL_KQ_CP_OK)
        goto done; /* nothing to sign without a history: a signature never starts one */
    if (drv->verify(&container, NULL) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID; /* a damaged history is refused, never replaced */
        goto done;
    }
    /* The head is re-read under the lock: another author's head is not this key's to sign. */
    st = drv->head_author(&container, head, sizeof head);
    if (st != KQ_FILE_OK || strcmp(head, author_key) != 0) {
        rc = st == KQ_FILE_FORKED ? FL_KQ_CP_FORKED : st == KQ_FILE_OK ? FL_KQ_CP_UNTRUSTED : FL_KQ_CP_INVALID;
        goto done;
    }
    st = drv->check_in(&container, author_key, created_at_utc, content.data, content.len, signing_secret,
                       &next, rev);
    if (st == KQ_FILE_FORKED) {
        rc = FL_KQ_CP_FORKED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    rc = store_container(container_path, &next);
done:
    fl_kq_buf_free(&content);
    fl_kq_buf_free(&container);
    fl_kq_buf_free(&next);
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_sign_head_with(const char *path, const char *author_key,
                                          const uint8_t signing_secret[32], const char *created_at_utc) {
    fl_kq_cp_result_t rc;

    KQ_LOCK();
    rc = fl_kq_cp_sign_head_with_unlocked(path, author_key, signing_secret, created_at_utc);
    KQ_UNLOCK();
    return rc;
}

/* ---- history bundle letters ---------------------------------------------------------------- */

fl_kq_cp_result_t fl_kq_cp_history_letters_with(const fl_kq_keydir_t *keys, const char *host_key,
                                                const uint8_t host_signing_secret[32], const char *requester_key,
                                                const char *path, fl_kq_buf_t *l15, fl_kq_buf_t *l17) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *host_box, *requester_box;
    char container_path[KQ_PATH_MAX];
    fl_kq_buf_t c = {0}, ev = {0};
    fl_kq_encoded_t sealed;
    uint8_t rev[32], did[16];
    uint32_t events = 0;
    fl_kq_cp_result_t rc;

    if (!l15 || !l17)
        return FL_KQ_CP_INVALID;
    memset(l15, 0, sizeof *l15);
    memset(l17, 0, sizeof *l17);
    memset(&sealed, 0, sizeof sealed);
    if (!keys || !host_key || !host_signing_secret || !requester_key || !path)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    host_box = keys->box_public(keys, host_key);
    requester_box = keys->box_public(keys, requester_key);
    if (!host_box || !requester_box)
        return FL_KQ_CP_NO_KEY;
    rc = fl_kq_cp_head(path, rev);
    if (rc != FL_KQ_CP_OK)
        return rc;
    rc = fl_kq_cp_reseal_with(keys, host_key, host_signing_secret, requester_key, requester_box, path, rev, &sealed);
    if (rc != FL_KQ_CP_OK)
        return rc;
    rc = load_container(path, container_path, sizeof container_path, &c);
    if (rc != FL_KQ_CP_OK)
        goto fail;
    if (drv->events(&c, &ev) != KQ_FILE_OK) { /* one line per event: the snapshot covers them all */
        rc = FL_KQ_CP_INVALID;
        goto fail;
    }
    for (size_t i = 0; i < ev.len; i++)
        events += ev.data[i] == '\n';
    if (drv->seal_snapshot(&c, events, host_key, host_signing_secret, host_box, requester_key, requester_box, l17,
                           did) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto fail;
    }
    *l15 = sealed.letter; /* the caller owns the letter now */
    memset(&sealed.letter, 0, sizeof sealed.letter);
    fl_kq_encoded_free(&sealed);
    fl_kq_buf_free(&c);
    fl_kq_buf_free(&ev);
    return FL_KQ_CP_OK;
fail:
    fl_kq_buf_free(l17);
    memset(l17, 0, sizeof *l17);
    fl_kq_encoded_free(&sealed);
    fl_kq_buf_free(&c);
    fl_kq_buf_free(&ev);
    return rc;
}
