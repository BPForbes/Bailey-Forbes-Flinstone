/* Sealed account keys in the freestanding image: one sealed signing key per account; nothing stays open.
 * The public half is kept beside it: the image's key directory for KeyQuorum file histories. */
#include "identity.h"
#include "kq_disk.h"
#include "kq_files.h"
#include "fl/driver/keyquorum.h"
#include "mem_domain.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int used;
    int restored; /* from the data disk: enroll keeps these keys instead of making new ones */
    char name[16];
    uint8_t sealed[FL_KQ_ACCOUNT_SEALED_LEN];
    uint8_t pub[32];
} kqi_t;

/* The record the data disk keeps for an account: its sealed key, then its public key. */
#define KQI_BLOB_LEN (FL_KQ_ACCOUNT_SEALED_LEN + 32)

static void kqi_persist(const kqi_t *k)
{
    uint8_t blob[KQI_BLOB_LEN];

    mem_domain_copy(blob, k->sealed, FL_KQ_ACCOUNT_SEALED_LEN);
    mem_domain_copy(blob + FL_KQ_ACCOUNT_SEALED_LEN, k->pub, 32);
    (void)fl_fs_disk_append(FL_DISK_KIND_ACCOUNT, k->name, blob, sizeof blob);
    mem_domain_secure_zero(blob, sizeof blob);
}

static kqi_t s_kqi[FL_FS_MAX_USERS];

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static int ci_equal(const char *a, const char *b)
{
    while (*a && *b) {
        if (lower(*a) != lower(*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

static void copy_name(char *dst, const char *src, unsigned cap)
{
    unsigned i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static kqi_t *kqi_find(const char *name)
{
    for (int i = 0; i < FL_FS_MAX_USERS; i++)
        if (s_kqi[i].used && ci_equal(s_kqi[i].name, name))
            return &s_kqi[i];
    return NULL;
}

static int seal_new(const fl_kq_driver_t *drv, const char *pw, uint8_t out[FL_KQ_ACCOUNT_SEALED_LEN],
                    uint8_t pub[32])
{
    uint8_t sec[32];
    size_t n = 0;
    int rc = -1;

    if (drv->account_keygen(sec, pub) == FL_KQ_ACCOUNT_OK &&
        drv->account_seal(pw, sec, out, FL_KQ_ACCOUNT_SEALED_LEN, &n) == FL_KQ_ACCOUNT_OK &&
        n == FL_KQ_ACCOUNT_SEALED_LEN)
        rc = 0;
    drv->wipe(sec, sizeof sec);
    return rc;
}

static int kqi_enroll(const char *creator, const char *name, const char *pw)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    kqi_t *k = NULL;

    (void)creator;
    if (!drv || !name || !name[0] || !pw || !pw[0])
        return -1;
    if ((k = kqi_find(name)) != NULL) {
        if (!k->restored)
            return -1;
        k->restored = 0; /* the boot restored this account's sealed key: keep it */
        return 0;
    }
    for (int i = 0; i < FL_FS_MAX_USERS && !k; i++)
        if (!s_kqi[i].used)
            k = &s_kqi[i];
    if (!k || seal_new(drv, pw, k->sealed, k->pub) != 0)
        return -1;
    copy_name(k->name, name, sizeof k->name);
    k->used = 1;
    kqi_persist(k);
    return 0;
}

/* A sealed account read back from the data disk, before the accounts are enrolled at boot. */
void fl_fs_kq_restore_account(const char *name, const uint8_t *blob, size_t len)
{
    kqi_t *k = name ? kqi_find(name) : NULL;

    if (len != KQI_BLOB_LEN || !name || !name[0])
        return;
    for (int i = 0; i < FL_FS_MAX_USERS && !k; i++)
        if (!s_kqi[i].used)
            k = &s_kqi[i];
    if (!k)
        return;
    k->used = 1;
    k->restored = 1;
    copy_name(k->name, name, sizeof k->name);
    mem_domain_copy(k->sealed, blob, FL_KQ_ACCOUNT_SEALED_LEN);
    mem_domain_copy(k->pub, blob + FL_KQ_ACCOUNT_SEALED_LEN, 32);
}

int fl_fs_kq_open_signing(const char *name, const char *pw, uint8_t sec[32])
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    kqi_t *k = name ? kqi_find(name) : NULL;

    if (!drv || !k || !pw || !sec)
        return -1;
    if (drv->account_open(pw, k->sealed, FL_KQ_ACCOUNT_SEALED_LEN, sec) == FL_KQ_ACCOUNT_OK)
        return 0;
    drv->wipe(sec, 32);
    return -1;
}

static int kqi_unlock(const char *name, const char *pw)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    uint8_t sec[32];
    int ok;

    if (!drv)
        return 0;
    ok = fl_fs_kq_open_signing(name, pw, sec) == 0;
    drv->wipe(sec, sizeof sec); /* nothing stays open */
    return ok;
}

static int kqi_change_password(const char *name, const char *pw)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    kqi_t *k = name ? kqi_find(name) : NULL;
    uint8_t fresh[FL_KQ_ACCOUNT_SEALED_LEN], pub[32];

    if (!drv || !k || !pw || !pw[0] || seal_new(drv, pw, fresh, pub) != 0)
        return -1;
    /* A new key: revisions signed with the old one no longer verify as this account's. */
    mem_domain_copy(k->sealed, fresh, sizeof fresh);
    mem_domain_copy(k->pub, pub, sizeof pub);
    kqi_persist(k);
    return 0;
}

static void kqi_forget(const char *name)
{
    kqi_t *k = name ? kqi_find(name) : NULL;

    if (k)
        mem_domain_secure_zero(k, sizeof *k); /* the sealed key and the used flag go together */
}

static const fl_fs_identity_kq_ops_t s_ops = {
    .enroll = kqi_enroll,
    .unlock = kqi_unlock,
    .switch_allowed = NULL, /* no extra refusal: switchuser asks the target's password */
    .change_password = kqi_change_password,
    .forget = kqi_forget,
};

/* The key directory the checkpoints read: an account's label is its name, its key the one sealed here. */
static const char *kd_resolve(const fl_kq_keydir_t *dir, const char *who)
{
    kqi_t *k = who ? kqi_find(who) : NULL;

    (void)dir;
    return k ? k->name : NULL;
}

static const unsigned char *kd_signing(const fl_kq_keydir_t *dir, const char *label)
{
    kqi_t *k = label ? kqi_find(label) : NULL;

    (void)dir;
    return k ? k->pub : NULL;
}

static const unsigned char *kd_box(const fl_kq_keydir_t *dir, const char *label)
{
    (void)dir, (void)label;
    return NULL; /* the image seals no box key: it delivers nothing */
}

static const fl_kq_keydir_t s_keydir = { kd_resolve, kd_signing, kd_box };

const fl_kq_keydir_t *fl_fs_kq_keydir(void)
{
    return &s_keydir;
}

void fl_fs_kq_bind_identity(void)
{
    fl_fs_identity_set_kq(&s_ops);
}
