#include "kq_checkpoint.h"
#include "kq_storage.h"
#include "contract_p2_heritage.h"
#include "server_shared_fs.h"
#include "fl/hex.h"
#include "fl/path.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "net_endian.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Largest file a co-signed delivery carries: half the letter cap, leaving room for the
 * container's ancestors and proofs. */
#define KQ_CONTENT_MAX (FL_KQ_LETTER_MAX / 2u)
/* Most labels a trust table holds (authors, signers, the owner). */
#define KQ_TRUST_MAX 32u
/* Most (revision, root) pairs remembered per file. */
/* Accepted (revision, history root) pairs kept in <file>.kqseen: the newest ones only. Each
 * accepted root extends the one before it, so the newest root still refuses forks and stale
 * histories; a revision the previously accepted container already holds is refused
 * separately (decode), so dropping old pairs never lets an old revision back in. */
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
    /* The reserved suffixes are one list, shared with the share landing. */
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
#define KQ_PATH_MAX FL_FS_PATH_MAX

/*
 * Every read and write below goes through the storage table (kq_storage.h): the placement layer
 * by default, the native shell's facade when it installs one. The checkpoints decide what a
 * delivery means; they never open a path themselves.
 */
static int def_read_file(void *ctx, const char *path, size_t max, uint8_t **data, size_t *len) {
    (void)ctx;
    return fl_fs_read_file(path, max, data, len);
}

static int def_write_atomic(void *ctx, const char *path, const uint8_t *data, size_t len) {
    (void)ctx;
    return fl_fs_write_file_atomic(path, data, len);
}

/* A permission error is "cannot tell", never "absent": a container that cannot be read must
 * not be replaced by a new one. */
static int def_exists(void *ctx, const char *path) {
    (void)ctx;
    if (access(path, F_OK) == 0)
        return 1;
    return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
}

/* Where the contained calls exist a directory that cannot be held is refused; names are then
 * never joined to a path. Otherwise (no *at calls) the path-based calls stand in. */
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

static const fl_kq_storage_t *s_storage = NULL;

const fl_kq_storage_t *fl_kq_storage_default(void) {
    return &s_default_storage;
}

const fl_kq_storage_t *fl_kq_cp_storage(void) {
    return s_storage ? s_storage : &s_default_storage;
}

const fl_kq_storage_t *fl_kq_cp_set_storage(const fl_kq_storage_t *storage) {
    const fl_kq_storage_t *was = fl_kq_cp_storage();

    s_storage = storage;
    return was;
}

/* A landing directory held for the length of one checkpoint. owned: close it at the end (a
 * caller that passed its own held directory keeps it). */
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

/* Splits dest into its directory and last name; false when the name part is empty. */
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

static int sidecar(const char *path, const char *suffix, char *out, size_t cap) {
    return snprintf(out, cap, "%s%s", path, suffix) < (int)cap ? 0 : -1;
}

/* The one path-based container read: the container beside path, through the storage table.
 * IO when it cannot be read (absent included), TOO_LARGE past the letter cap. */
static fl_kq_cp_result_t load_container(const char *path, char *container_path, size_t cap,
                                        fl_kq_buf_t *c) {
    int rc;

    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, cap) != 0)
        return FL_KQ_CP_INVALID;
    rc = fl_kq_cp_storage()->read_file(fl_kq_cp_storage()->ctx, container_path, FL_KQ_LETTER_MAX,
                                       &c->data, &c->len);
    if (rc != 0)
        return rc == -2 ? FL_KQ_CP_TOO_LARGE : FL_KQ_CP_IO;
    return FL_KQ_CP_OK;
}

/* ---- trust table --------------------------------------------------------- */

typedef struct {
    kq_file_key_t keys[KQ_TRUST_MAX];
    char          labels[KQ_TRUST_MAX][FL_HERITAGE_KEY_ID_MAX];
    size_t        n;
    fl_kq_buf_t   signers;
} trust_table_t;

/* The signing keys this account store holds for every label the container names; a
 * label it does not know is left out (its signatures then do not count). `extra_label`
 * and `extra_key` add one key the caller vouches for (a sender's key from the host). */
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

fl_kq_cp_result_t fl_kq_cp_encode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const char *created_at_utc, fl_kq_encoded_t *out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *sender_box;
    char container_path[4096];
    fl_kq_buf_t content = {0};
    fl_kq_buf_t container = {0};
    fl_kq_buf_t next = {0};
    trust_table_t table;
    kq_file_trust_t trust;
    uint8_t rev[32];
    fl_kq_cp_result_t rc = FL_KQ_CP_OK;
    const fl_kq_storage_t *store;
    int have_container;
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
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;

    store = fl_kq_cp_storage();
    st = store->read_file(store->ctx, path, KQ_CONTENT_MAX, &content.data, &content.len);
    if (st == -2)
        return FL_KQ_CP_TOO_LARGE;
    if (st != 0)
        return FL_KQ_CP_IO;
    /* The file's container: the one beside it, or a new one owned by the sender. A container
     * that cannot be looked at is an error, never a reason to start a new history over it. */
    have_container = store->exists(store->ctx, container_path);
    if (have_container < 0) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    if (have_container == 0) {
        uint8_t file_id[16];

        if (fl_random_bytes(file_id, sizeof file_id) != 0 ||
            drv->file_new(file_id, fl_fs_basename(path), sender_key, &container) != KQ_FILE_OK) {
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

    st = drv->check_in(&container, sender_key, created_at_utc, content.data, content.len,
                       sender_signing_secret, &next, rev);
    if (st == KQ_FILE_FORKED) {
        rc = FL_KQ_CP_FORKED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
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
    if (store->write_atomic(store->ctx, container_path, next.data, next.len) != 0) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    snprintf(out->file_name, sizeof out->file_name, "%s", fl_fs_basename(path));

done:
    if (rc != FL_KQ_CP_OK)
        fl_kq_encoded_free(out);
    fl_kq_buf_free(&content);
    fl_kq_buf_free(&container);
    fl_kq_buf_free(&next);
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
    /* KeyQuorum refuses the offer's own author. */
    return drv->cosign(offer, me_key, signing_secret, sig) == KQ_FILE_OK ? FL_KQ_CP_OK
                                                                         : FL_KQ_CP_NOT_RECIPIENT;
}

/* Loads the newest KQ_SEEN_KEEP pairs; a longer file (older builds kept more) is read from
 * its tail. A missing file is an empty record; a damaged one (not whole pairs) is -1. */
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

/*
 * Whether the previously accepted container beside the file already holds rev. It must be
 * the container that was accepted last: it verifies and its history root is the newest
 * accepted root. Anything else fails closed, since "absent" would let an old revision back.
 *   1 held, 0 not held, -1 unreadable or missing, -2 damaged or not the accepted copy.
 */
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

/*
 * The decode steps, over a landing directory the caller holds (landp, the leaf's name inside
 * it). Every read and write of the delivery goes through that directory and the storage table.
 */
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
    /* Never overwrite a file this store does not track. */
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
    /* The letter must carry exactly the revision that was offered and co-signed. */
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
    /* Only the newest pairs are kept, so "adds a revision" is also checked against everything
     * accepted before: a revision the local container holds is never newer. */
    if (st == KQ_FILE_NEWER) {
        int held = nseen ? local_holds(drv, landp, container_name, seen[nseen - 1] + 32, rev) : -2;

        if (held != 0) {
            rc = held > 0 ? FL_KQ_CP_NOT_NEWER : held == -1 ? FL_KQ_CP_IO : FL_KQ_CP_INVALID;
            goto done;
        }
        /* A newer copy goes out only when the editor's supervisor countersigned it: the
         * revision carries the permit, or the recipient keeps the copy it has. */
        if (drv->permits_newer(&cosigned, rev, &trust) != KQ_FILE_OK) {
            rc = FL_KQ_CP_NEEDS_EDITOR_PERMIT;
            goto done;
        }
    }

    if (drv->payload(&cosigned, rev, &payload) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    /* The content, its history and the record that they were accepted land together, or
     * not at all: a later retry must not find a half-installed delivery. */
    memcpy(pair, rev, 32);
    memcpy(pair + 32, root, 32);
    {
        /* The newest KQ_SEEN_KEEP - 1 pairs and this one: the record never grows past the cap. */
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
    /* The quarantine gate applies here as on the share's own landings (either spelling). */
    if (fl_server_shared_path_is_expired_quarantine(land_dir) ||
        fl_server_shared_path_is_expired_quarantine(dest_path))
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
    if (fl_server_shared_path_is_expired_quarantine(dir->dir) ||
        fl_server_shared_path_is_expired_quarantine(dest))
        return FL_KQ_CP_IO;
    /* The caller's own handle: used, never closed here, and never opened again by path. */
    land.st = fl_kq_cp_storage();
    land.dir = *dir;
    land.owned = 0;
    return decode_core(keys, sender_key, sender_signing_public, me_key, box_secret, letter, offer,
                       my_sig, &land, leaf);
}

/* ---- history and the editor's permit (the local container) ------------- */

/* Names of the history's wire codes, for display only: a code is what the file stores and what
 * KeyQuorum reads; these strings are never parsed back. They come from KeyQuorum through the
 * driver (kq_file_event_name / kq_file_outcome_name), so there is no table here to fall behind
 * its enums. A code the library does not know, or no driver, prints as "?". */
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

/* One history line (see keyquorum_files.h) as text: sequence, time, type name, outcome name,
 * actor, revision and details. A malformed line is copied as it is. */
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
    char container_path[4096];
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

fl_kq_cp_result_t fl_kq_cp_head(const char *path, uint8_t rev[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[4096];
    fl_kq_buf_t c = {0};
    fl_kq_cp_result_t out = FL_KQ_CP_OK;
    int rc;

    if (!path || !rev)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    out = load_container(path, container_path, sizeof container_path, &c);
    if (out != FL_KQ_CP_OK)
        return out;
    rc = drv->head(&c, rev);
    if (rc == KQ_FILE_FORKED)
        out = FL_KQ_CP_FORKED;
    else if (rc != KQ_FILE_OK)
        out = FL_KQ_CP_INVALID;
    fl_kq_buf_free(&c);
    return out;
}

fl_kq_cp_result_t fl_kq_cp_head_author(const char *path, char *out, size_t cap) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[4096];
    fl_kq_buf_t c = {0};
    fl_kq_cp_result_t res;
    int rc;

    if (!path || !out || cap == 0)
        return FL_KQ_CP_INVALID;
    out[0] = '\0';
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    res = load_container(path, container_path, sizeof container_path, &c);
    if (res != FL_KQ_CP_OK)
        return res;
    rc = drv->head_author(&c, out, cap);
    if (rc == KQ_FILE_FORKED)
        res = FL_KQ_CP_FORKED;
    else if (rc != KQ_FILE_OK)
        res = FL_KQ_CP_INVALID; /* no revision, no room, malformed: nobody is named */
    fl_kq_buf_free(&c);
    if (res != FL_KQ_CP_OK)
        out[0] = '\0';
    return res;
}

/* Sealed letter for revision rev already in the container beside path (sender). Nothing is
 * checked in or written: this is how a permitted newer copy goes out, after the supervisor's
 * countersignature is in that container. */
fl_kq_cp_result_t fl_kq_cp_reseal_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_secret[32],
                                       const char *recipient_key,
                                       const uint8_t recipient_box_public[32], const char *path,
                                       const uint8_t rev[32], fl_kq_encoded_t *out) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *sender_box;
    char container_path[4096];
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

fl_kq_cp_result_t fl_kq_cp_permit_with(const fl_kq_keydir_t *keys, const char *supervisor_key,
                                       const uint8_t supervisor_secret[32], const char *path,
                                       const uint8_t rev[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[4096];
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
    /* The author's signature must verify under the keys this registry holds before the
     * supervisor adds to it: a countersignature never backs an unverified author. */
    trust_build(drv, keys, &c, NULL, NULL, &table, &trust);
    rc = drv->countersign(&c, rev, supervisor_key, supervisor_secret, &trust, &next);
    if (rc != KQ_FILE_OK) {
        out = FL_KQ_CP_INVALID;
        goto done;
    }
    if (fl_kq_cp_storage()->write_atomic(fl_kq_cp_storage()->ctx, container_path, next.data,
                                         next.len) != 0)
        out = FL_KQ_CP_IO;

done:
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&c);
    return out;
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

int fl_kq_cp_report_path(const char *dir, const uint8_t ref[32], char *out, size_t cap) {
    char hex[65];

    if (!dir || !dir[0] || !ref || !out)
        return -1;
    fl_hex_encode(ref, 32, hex);
    return snprintf(out, cap, "%s/%s%s", dir, hex, FL_KQ_REPORT_SUFFIX) < (int)cap ? 0 : -1;
}

fl_kq_cp_result_t fl_kq_cp_report_write_in(uint16_t code, uint8_t class_code, uint8_t step,
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
    fl_hex_encode(ref, 32, name);
    memcpy(name + 64, FL_KQ_REPORT_SUFFIX, sizeof FL_KQ_REPORT_SUFFIX);
    /* Every step inside the held directory: a directory swapped after the caller took the
     * handle is never followed. An existing report is kept as it is and not counted again. */
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
    /* No free text: the code, class and labels are KeyQuorum's closed sets, checked there. */
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

fl_kq_cp_result_t fl_kq_cp_inject_report_with(const fl_kq_keydir_t *keys, const char *reporter_key,
                                              const char *report_path, const uint8_t ref[32],
                                              const char *path) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *reporter_public;
    char container_path[4096];
    fl_kq_buf_t c = {0}, kqer = {0}, next = {0};
    fl_kq_cp_result_t out;
    int rc;

    if (!keys || !reporter_key || !report_path || !ref || !path)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    reporter_public = keys->signing_public(keys, reporter_key);
    if (!reporter_public)
        return FL_KQ_CP_NO_KEY;
    rc = fl_kq_cp_storage()->read_file(fl_kq_cp_storage()->ctx, report_path, FL_KQ_REPORT_MAX,
                                       &kqer.data, &kqer.len);
    if (rc != 0)
        return rc == -2 ? FL_KQ_CP_TOO_LARGE : FL_KQ_CP_IO;
    out = load_container(path, container_path, sizeof container_path, &c);
    if (out != FL_KQ_CP_OK)
        goto done;
    /* KeyQuorum checks the signature under the reporter's key from this registry and that the
     * report names the artifact this caller holds; anything else writes nothing. */
    rc = drv->error_inject(&c, &kqer, reporter_public, ref, &next);
    if (rc != KQ_FILE_OK) {
        out = FL_KQ_CP_BAD_SIGNATURE;
        goto done;
    }
    if (fl_kq_cp_storage()->write_atomic(fl_kq_cp_storage()->ctx, container_path, next.data,
                                         next.len) != 0)
        out = FL_KQ_CP_IO;

done:
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&kqer);
    fl_kq_buf_free(&c);
    return out;
}
