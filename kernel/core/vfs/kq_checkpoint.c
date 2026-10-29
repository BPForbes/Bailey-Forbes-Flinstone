#include "kq_checkpoint.h"
#include "contract_p2_heritage.h"
#include "server_shared_fs.h"
#include "fl/path.h"
#include "fl/random.h"

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

typedef struct {
    const char    *path; /* a plain name inside the landing directory */
    const uint8_t *data;
    size_t         len;
} kq_out_t;

/*
 * The landing directory. Where the contained calls exist it is held as an fd and every name
 * is used inside it, so a link planted at a name is never followed; the directory's own path
 * is trusted. Otherwise (no *at calls, or the directory cannot be opened) names are joined
 * to the directory's path and the path-based calls are used as before.
 */
typedef struct {
    int         fd;  /* -1: path-based */
    const char *dir;
} kq_land_t;

static int land_name(const kq_land_t *l, const char *name, char *out, size_t cap) {
    if (strcmp(l->dir, ".") == 0 || l->dir[0] == '\0')
        return snprintf(out, cap, "%s", name) < (int)cap ? 0 : -1;
    return snprintf(out, cap, "%s/%s", l->dir, name) < (int)cap ? 0 : -1;
}

static void land_open(kq_land_t *l, const char *dir) {
    l->dir = dir;
    l->fd = -1;
    if (fl_fs_contained_available())
        l->fd = fl_fs_dir_open(dir); /* a failure stays -1 and is refused by land_ready */
}

/* The path-based calls are for platforms without the contained ones only. */
static int land_ready(const kq_land_t *l) {
    return l->fd >= 0 || !fl_fs_contained_available();
}

static void land_close(kq_land_t *l) {
    if (l->fd >= 0)
        close(l->fd);
    l->fd = -1;
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

static int land_stage(const kq_land_t *l, const char *name, const uint8_t *d, size_t n) {
    char p[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_stage_file_in(l->fd, name, d, n);
    return land_name(l, name, p, sizeof p) == 0 ? fl_fs_stage_file(p, d, n) : -1;
}

static int land_write(const kq_land_t *l, const char *name, const uint8_t *d, size_t n) {
    char p[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_write_file_atomic_in(l->fd, name, d, n);
    return land_name(l, name, p, sizeof p) == 0 ? fl_fs_write_file_atomic(p, d, n) : -1;
}

static int land_read(const kq_land_t *l, const char *name, uint8_t **d, size_t *n) {
    char p[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_read_file_in(l->fd, name, FL_KQ_LETTER_MAX, d, n);
    if (land_name(l, name, p, sizeof p) != 0)
        return -1;
    return fl_fs_read_file(p, FL_KQ_LETTER_MAX, d, n);
}

static int land_unlink(const kq_land_t *l, const char *name) {
    char p[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_unlink_in(l->fd, name);
    return land_name(l, name, p, sizeof p) == 0 ? unlink(p) : -1;
}

static int land_rename(const kq_land_t *l, const char *from, const char *to) {
    char a[KQ_PATH_MAX], b[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_rename_in(l->fd, from, to);
    if (land_name(l, from, a, sizeof a) != 0 || land_name(l, to, b, sizeof b) != 0)
        return -1;
    return rename(a, b);
}

static int land_link(const kq_land_t *l, const char *from, const char *to) {
    char a[KQ_PATH_MAX], b[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_link_in(l->fd, from, to);
    if (land_name(l, from, a, sizeof a) != 0 || land_name(l, to, b, sizeof b) != 0)
        return -1;
    return link(a, b);
}

/* 1 present (a link counts), 0 absent, -1 cannot tell. */
static int land_exists(const kq_land_t *l, const char *name) {
    char p[KQ_PATH_MAX];

    if (l->fd >= 0)
        return fl_fs_exists_in(l->fd, name);
    if (land_name(l, name, p, sizeof p) != 0)
        return -1;
    return access(p, F_OK) == 0 ? 1 : 0;
}

static int part_name(const char *name, char *out, size_t cap) {
    return snprintf(out, cap, "%s.part", name) < (int)cap ? 0 : -1;
}

static int bak_name(const char *name, char *out, size_t cap) {
    return snprintf(out, cap, "%s.kqbak", name) < (int)cap ? 0 : -1;
}

/*
 * Keeps name as bak. A hard link costs nothing; a file system without links (the browser
 * lab's in-memory one refuses them) gets a copy instead, wiped from memory after.
 */
static int backup_file(const kq_land_t *l, const char *name, const char *bak) {
    fl_kq_buf_t copy = {0};
    int rc;

    if (land_link(l, name, bak) == 0)
        return 0;
    if (land_read(l, name, &copy.data, &copy.len) != 0)
        return -1;
    rc = land_write(l, bak, copy.data, copy.len);
    fl_kq_buf_free(&copy);
    return rc;
}

/*
 * Replaces every file in out[] or none of them. All parts are staged first; then each
 * existing file is kept as <name>.kqbak (backup_file) and the parts are renamed into
 * place. If a rename fails, the files already replaced are restored from their backups
 * (or removed when they did not exist), so a failure leaves the files as they were.
 */
static int commit_all(const kq_land_t *l, const kq_out_t *out, size_t n) {
    char part[KQ_PATH_MAX];
    char bak[KQ_PATH_MAX];
    int had[4] = {0};
    size_t done = 0;
    size_t i;
    int rc = 0;

    if (n > sizeof had / sizeof had[0])
        return -1;
    for (i = 0; i < n; i++) {
        if (land_stage(l, out[i].path, out[i].data, out[i].len) != 0)
            break;
    }
    if (i < n) {
        while (i-- > 0) {
            if (part_name(out[i].path, part, sizeof part) == 0)
                land_unlink(l, part);
        }
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (bak_name(out[i].path, bak, sizeof bak) != 0) {
            rc = -1;
            break;
        }
        land_unlink(l, bak);
        had[i] = land_exists(l, out[i].path) == 1;
        if (had[i] && backup_file(l, out[i].path, bak) != 0) {
            rc = -1;
            break;
        }
    }
    for (; rc == 0 && done < n; done++) {
        if (part_name(out[done].path, part, sizeof part) != 0 ||
            land_rename(l, part, out[done].path) != 0)
            rc = -1;
    }
    if (rc != 0) {
        /* Put back what was replaced; a file that did not exist before is removed. */
        size_t replaced = done ? done - 1 : 0;
        for (i = 0; i < replaced; i++) {
            if (bak_name(out[i].path, bak, sizeof bak) != 0)
                continue;
            if (had[i])
                (void)land_rename(l, bak, out[i].path);
            else
                land_unlink(l, out[i].path);
        }
        for (i = 0; i < n; i++) {
            if (part_name(out[i].path, part, sizeof part) == 0)
                land_unlink(l, part);
        }
    }
    for (i = 0; i < n; i++) {
        if (bak_name(out[i].path, bak, sizeof bak) == 0)
            land_unlink(l, bak);
    }
    return rc;
}

static int sidecar(const char *path, const char *suffix, char *out, size_t cap) {
    return snprintf(out, cap, "%s%s", path, suffix) < (int)cap ? 0 : -1;
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

    st = fl_fs_read_file(path, KQ_CONTENT_MAX, &content.data, &content.len);
    if (st == -2)
        return FL_KQ_CP_TOO_LARGE;
    if (st != 0)
        return FL_KQ_CP_IO;
    /* The file's container: the one beside it, or a new one owned by the sender. */
    if (access(container_path, F_OK) != 0) {
        uint8_t file_id[16];

        if (fl_random_bytes(file_id, sizeof file_id) != 0 ||
            drv->file_new(file_id, fl_fs_basename(path), sender_key, &container) != KQ_FILE_OK) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
    } else if (fl_fs_read_file(container_path, FL_KQ_LETTER_MAX, &container.data,
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
    if (fl_fs_write_file_atomic(container_path, next.data, next.len) != 0) {
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
    FILE *f;
    long size;
    size_t want;
    uint8_t (*buf)[64];

    *seen = NULL;
    *n = 0;
    if (l->fd >= 0) {
        int fd = fl_fs_open_read_in(l->fd, name);

        if (fd < 0)
            return errno == ENOENT ? 0 : -1;
        f = fdopen(fd, "rb");
        if (!f) {
            close(fd);
            return -1;
        }
    } else {
        char path[KQ_PATH_MAX];

        if (land_name(l, name, path, sizeof path) != 0)
            return -1;
        f = fopen(path, "rb");
        if (!f)
            return access(path, F_OK) == 0 ? -1 : 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || size % 64 != 0) {
        fclose(f);
        return -1;
    }
    want = (size_t)size / 64u;
    if (want > KQ_SEEN_KEEP)
        want = KQ_SEEN_KEEP;
    if (want == 0) {
        fclose(f);
        return 0;
    }
    buf = (uint8_t (*)[64])malloc(want * 64u);
    if (!buf || fseek(f, size - (long)(want * 64u), SEEK_SET) != 0 ||
        fread(buf, 64u, want, f) != want) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *seen = buf;
    *n = want;
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

fl_kq_cp_result_t fl_kq_cp_decode_with(const fl_kq_keydir_t *keys, const char *sender_key,
                                       const uint8_t sender_signing_public[32],
                                       const char *me_key, const uint8_t box_secret[32],
                                       const fl_kq_buf_t *letter, const kq_file_offer_t *offer,
                                       const uint8_t my_sig[64], const char *dest_path) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char land_dir[KQ_PATH_MAX];
    const char *leaf = NULL;
    char container_name[KQ_PATH_MAX];
    char seen_name[KQ_PATH_MAX];
    kq_land_t land = {-1, NULL};
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

    if (!keys || !sender_key || !sender_signing_public || !me_key || !box_secret || !letter ||
        !offer || !my_sig || !dest_path)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    if (land_split(dest_path, land_dir, sizeof land_dir, &leaf) != 0 ||
        sidecar(leaf, FL_KQ_CONTAINER_SUFFIX, container_name, sizeof container_name) != 0 ||
        sidecar(leaf, FL_KQ_SEEN_SUFFIX, seen_name, sizeof seen_name) != 0)
        return FL_KQ_CP_INVALID;
    /* The quarantine gate applies here as on the share's own landings (either spelling). */
    if (fl_server_shared_path_is_expired_quarantine(land_dir) ||
        fl_server_shared_path_is_expired_quarantine(dest_path))
        return FL_KQ_CP_IO;
    land_open(&land, land_dir);
    if (!land_ready(&land))
        return FL_KQ_CP_IO;
    /* Never overwrite a file this store does not track. */
    if (land_exists(&land, leaf) == 1 && land_exists(&land, seen_name) != 1) {
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

    if (load_seen(&land, seen_name, &seen, &nseen) != 0) {
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
        int held = nseen ? local_holds(drv, &land, container_name, seen[nseen - 1] + 32, rev) : -2;

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
        uint8_t *all = (uint8_t *)malloc(len);
        kq_out_t out[3];

        if (!all) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
        if (keep)
            memcpy(all, seen + (nseen - keep), keep * 64u);
        memcpy(all + keep * 64u, pair, 64u);
        out[0] = (kq_out_t){leaf, payload.data, payload.len};
        out[1] = (kq_out_t){container_name, cosigned.data, cosigned.len};
        out[2] = (kq_out_t){seen_name, all, len};
        if (commit_all(&land, out, 3) != 0)
            rc = FL_KQ_CP_IO;
        free(all);
    }

done:
    land_close(&land);
    free(seen);
    fl_kq_buf_free(&opened);
    fl_kq_buf_free(&cosigned);
    fl_kq_buf_free(&payload);
    return rc;
}

/* ---- history and the editor's permit (the local container) ------------- */

/* Names of the history's wire codes, for display only: a code is what the file stores and
 * what KeyQuorum reads; these strings are never parsed back. Generated from KeyQuorum's
 * src/file_history/event.rs (type and outcome codes). A code past the table prints as "?". */
static const char *const kq_event_names[] = {
    "TrackingStarted",
    "EditCheckedIn",
    "RevisionSigned",
    "CountersignatureAdded",
    "PolicyDecision",
    "ShareAttempted",
    "ShareDelivered",
    "FileExpired",
    "ContentDestroyed",
    "TamperDetected",
    "AutoMergeAttempted",
    "AutoMergeFastForward",
    "AutoMergeEquivalent",
    "AutoMergeClean",
    "AutoMergeBlocked",
    "AutoMergeRequiresHuman",
    "HistoryForkDetected",
    "ContentConflictDetected",
    "ConflictReviewAssigned",
    "ConflictReviewEscalated",
    "BridgeUsed",
    "ConflictUnresolved",
    "HistoryImported",
    "QuorumUnlockAttempted",
    "PasswordUnlockAttempted",
    "ExpiredAccessAttempt",
    "GateLinked",
    "ShareLinkCreated",
    "ShareLinkRedeemed",
    "ShareLinkRevoked",
    "ExpiryScheduled",
    "FileRenamed",
    "RevisionFinalized",
    "ConflictResolved",
    "MergeRejected",
    "RevisionCheckedOut",
    "VerificationRun",
    "HistoryExported",
    "FileRequested",
    "ChangeRequested",
    "RequestAnswered",
    "LetterQueued",
    "LetterSent",
    "LetterDropped",
    "LetterRefused",
    "LetterReceived",
    "LetterOpened",
};

static const char *const kq_outcome_names[] = {
    "Success",
    "Failure",
    "Denied",
    "Info",
};

const char *fl_kq_cp_event_name(uint8_t code) {
    if (code == 0 || code > sizeof kq_event_names / sizeof kq_event_names[0])
        return "?";
    return kq_event_names[code - 1u];
}

const char *fl_kq_cp_outcome_name(uint8_t code) {
    if (code == 0 || code > sizeof kq_outcome_names / sizeof kq_outcome_names[0])
        return "?";
    return kq_outcome_names[code - 1u];
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
    int rc;

    if (!lines || !path)
        return FL_KQ_CP_INVALID;
    lines->data = NULL;
    lines->len = 0;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;
    rc = fl_fs_read_file(container_path, FL_KQ_LETTER_MAX, &c.data, &c.len);
    if (rc != 0)
        return rc == -2 ? FL_KQ_CP_TOO_LARGE : FL_KQ_CP_IO;
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
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;
    if (fl_fs_read_file(container_path, FL_KQ_LETTER_MAX, &c.data, &c.len) != 0)
        return FL_KQ_CP_IO;
    rc = drv->head(&c, rev);
    if (rc == KQ_FILE_FORKED)
        out = FL_KQ_CP_FORKED;
    else if (rc != KQ_FILE_OK)
        out = FL_KQ_CP_INVALID;
    fl_kq_buf_free(&c);
    return out;
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
    if (fl_fs_read_file(container_path, FL_KQ_LETTER_MAX, &container.data, &container.len) != 0)
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
    if (fl_fs_read_file(container_path, FL_KQ_LETTER_MAX, &c.data, &c.len) != 0)
        return FL_KQ_CP_IO;
    /* The author's signature must verify under the keys this registry holds before the
     * supervisor adds to it: a countersignature never backs an unverified author. */
    trust_build(drv, keys, &c, NULL, NULL, &table, &trust);
    rc = drv->countersign(&c, rev, supervisor_key, supervisor_secret, &trust, &next);
    if (rc != KQ_FILE_OK) {
        out = FL_KQ_CP_INVALID;
        goto done;
    }
    if (fl_fs_write_file_atomic(container_path, next.data, next.len) != 0)
        out = FL_KQ_CP_IO;

done:
    fl_kq_buf_free(&next);
    fl_kq_buf_free(&c);
    return out;
}

