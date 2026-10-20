#include "kq_checkpoint.h"

#include <errno.h>
#include <fcntl.h>
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
#define KQ_SEEN_MAX 1024u

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
    case FL_KQ_CP_INVALID:
    default:
        return "malformed delivery";
    }
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

static int read_all(const char *path, size_t max, fl_kq_buf_t *out) {
    FILE *f = fopen(path, "rb");
    long n;

    out->data = NULL;
    out->len = 0;
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (size_t)n > max) {
        fclose(f);
        return (n >= 0 && (size_t)n > max) ? -2 : -1;
    }
    out->data = (uint8_t *)malloc((size_t)n ? (size_t)n : 1u);
    if (!out->data || fread(out->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        fl_kq_buf_free(out);
        return -1;
    }
    out->len = (size_t)n;
    fclose(f);
    return 0;
}

/* Write through a sibling and rename, so a reader never sees half a file. */
static int write_atomic(const char *path, const uint8_t *data, size_t len) {
    char tmp[4096 + 16];
    int fd;
    size_t off = 0;

    if (snprintf(tmp, sizeof tmp, "%s.part", path) >= (int)sizeof tmp)
        return -1;
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) {
            close(fd);
            unlink(tmp);
            return -1;
        }
        off += (size_t)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0 || rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int sidecar(const char *path, const char *suffix, char *out, size_t cap) {
    return snprintf(out, cap, "%s%s", path, suffix) < (int)cap ? 0 : -1;
}

static int random_bytes(uint8_t *out, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    size_t got = 0;

    if (fd < 0)
        return -1;
    while (got < n) {
        ssize_t r = read(fd, out + got, n - got);
        if (r <= 0) {
            close(fd);
            return -1;
        }
        got += (size_t)r;
    }
    close(fd);
    return 0;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
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
static void trust_build(const fl_kq_driver_t *drv, const fl_user_db_t *users,
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
                pub = fl_user_db_signing_public(users, label);
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

fl_kq_cp_result_t fl_kq_cp_encode(const fl_user_db_t *users, const char *sender_key,
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

    if (!out || !users || !sender_key || !sender_signing_secret || !recipient_key ||
        !recipient_box_public || !path || !created_at_utc)
        return FL_KQ_CP_INVALID;
    memset(out, 0, sizeof *out);
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    sender_box = fl_user_db_box_public(users, sender_key);
    if (!sender_box || !fl_user_db_signing_public(users, sender_key))
        return FL_KQ_CP_NO_KEY;
    if (sidecar(path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0)
        return FL_KQ_CP_INVALID;

    st = read_all(path, KQ_CONTENT_MAX, &content);
    if (st == -2)
        return FL_KQ_CP_TOO_LARGE;
    if (st != 0)
        return FL_KQ_CP_IO;
    /* The file's container: the one beside it, or a new one owned by the sender. */
    if (access(container_path, F_OK) != 0) {
        uint8_t file_id[16];

        if (random_bytes(file_id, sizeof file_id) != 0 ||
            drv->file_new(file_id, base_name(path), sender_key, &container) != KQ_FILE_OK) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
    } else if (read_all(container_path, FL_KQ_LETTER_MAX, &container) != 0 ||
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
    trust_build(drv, users, &next, NULL, NULL, &table, &trust);
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
    if (write_atomic(container_path, next.data, next.len) != 0) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    snprintf(out->file_name, sizeof out->file_name, "%s", base_name(path));

done:
    if (rc != FL_KQ_CP_OK)
        fl_kq_encoded_free(out);
    fl_kq_buf_free(&content);
    fl_kq_buf_free(&container);
    fl_kq_buf_free(&next);
    return rc;
}

fl_kq_cp_result_t fl_kq_cp_admit_offer(const fl_user_db_t *users, const char *author_key,
                                       const kq_file_offer_t *offer) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *pub;

    if (!users || !author_key || !offer)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    pub = fl_user_db_signing_public(users, author_key);
    if (!pub)
        return FL_KQ_CP_NO_KEY;
    return drv->offer_verify(offer, author_key, pub) == KQ_FILE_OK ? FL_KQ_CP_OK
                                                                    : FL_KQ_CP_BAD_SIGNATURE;
}

fl_kq_cp_result_t fl_kq_cp_admit_cosign(const fl_user_db_t *users, const char *recipient_key,
                                        const kq_file_offer_t *offer, const uint8_t sig[64]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const unsigned char *pub;

    if (!users || !recipient_key || !offer || !sig)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    pub = fl_user_db_signing_public(users, recipient_key);
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

static int load_seen(const char *path, uint8_t (**seen)[64], size_t *n) {
    fl_kq_buf_t raw = {0};

    *seen = NULL;
    *n = 0;
    if (read_all(path, KQ_SEEN_MAX * 64u, &raw) != 0)
        return access(path, F_OK) == 0 ? -1 : 0;
    if (raw.len % 64u != 0) {
        fl_kq_buf_free(&raw);
        return -1;
    }
    *n = raw.len / 64u;
    *seen = (uint8_t (*)[64])raw.data;
    return 0;
}

fl_kq_cp_result_t fl_kq_cp_decode(const fl_user_db_t *users, const char *sender_key,
                                  const uint8_t sender_signing_public[32], const char *me_key,
                                  const uint8_t box_secret[32], const fl_kq_buf_t *letter,
                                  const kq_file_offer_t *offer, const uint8_t my_sig[64],
                                  const char *dest_path) {
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    char container_path[4096];
    char seen_path[4096];
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

    if (!users || !sender_key || !sender_signing_public || !me_key || !box_secret || !letter ||
        !offer || !my_sig || !dest_path)
        return FL_KQ_CP_INVALID;
    if (!drv)
        return FL_KQ_CP_NO_DRIVER;
    if (sidecar(dest_path, FL_KQ_CONTAINER_SUFFIX, container_path, sizeof container_path) != 0 ||
        sidecar(dest_path, FL_KQ_SEEN_SUFFIX, seen_path, sizeof seen_path) != 0)
        return FL_KQ_CP_INVALID;
    /* Never overwrite a file this store does not track. */
    if (access(dest_path, F_OK) == 0 && access(seen_path, F_OK) != 0)
        return FL_KQ_CP_EXISTS;

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
    trust_build(drv, users, &cosigned, sender_key, sender_signing_public, &table, &trust);
    st = drv->readiness(&cosigned, rev, me_key, &trust, &have, &need);
    if (st == KQ_FILE_FORKED) {
        rc = FL_KQ_CP_FORKED;
        goto done;
    }
    if (st != KQ_FILE_OK) {
        rc = FL_KQ_CP_BELOW_THRESHOLD;
        goto done;
    }

    if (load_seen(seen_path, &seen, &nseen) != 0) {
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

    if (drv->payload(&cosigned, rev, &payload) != KQ_FILE_OK) {
        rc = FL_KQ_CP_INVALID;
        goto done;
    }
    /* Content and history first, then the record that they were accepted. */
    if (write_atomic(dest_path, payload.data, payload.len) != 0 ||
        write_atomic(container_path, cosigned.data, cosigned.len) != 0) {
        rc = FL_KQ_CP_IO;
        goto done;
    }
    memcpy(pair, rev, 32);
    memcpy(pair + 32, root, 32);
    {
        size_t len = (nseen + 1u) * 64u;
        uint8_t *all = (uint8_t *)malloc(len);

        if (!all) {
            rc = FL_KQ_CP_IO;
            goto done;
        }
        if (nseen)
            memcpy(all, seen, nseen * 64u);
        memcpy(all + nseen * 64u, pair, 64u);
        if (write_atomic(seen_path, all, len) != 0)
            rc = FL_KQ_CP_IO;
        free(all);
    }

done:
    free(seen);
    fl_kq_buf_free(&opened);
    fl_kq_buf_free(&cosigned);
    fl_kq_buf_free(&payload);
    return rc;
}
