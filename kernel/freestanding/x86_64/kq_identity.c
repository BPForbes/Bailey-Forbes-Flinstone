/*
 * FL-14 prototype (scratchpad copy only): sealed account keys in the freestanding image.
 * Modelled on tools/browser-lab/wasm/lab_kq.c id_enroll / id_unlock / id_change_password / id_forget,
 * but each account keeps only one sealed signing key (81 bytes) and nothing stays open.
 */
#include "identity.h"
#include "fl/driver/keyquorum.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int used;
    char name[16];
    uint8_t sealed[FL_KQ_ACCOUNT_SEALED_LEN];
} kqi_t;

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

static int seal_new(const fl_kq_driver_t *drv, const char *pw, uint8_t out[FL_KQ_ACCOUNT_SEALED_LEN])
{
    uint8_t sec[32], pub[32];
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
    if (!drv || !name || !name[0] || !pw || !pw[0] || kqi_find(name))
        return -1;
    for (int i = 0; i < FL_FS_MAX_USERS && !k; i++)
        if (!s_kqi[i].used)
            k = &s_kqi[i];
    if (!k || seal_new(drv, pw, k->sealed) != 0)
        return -1;
    copy_name(k->name, name, sizeof k->name);
    k->used = 1;
    return 0;
}

static int kqi_unlock(const char *name, const char *pw)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    kqi_t *k = name ? kqi_find(name) : NULL;
    uint8_t sec[32];
    int ok;

    if (!drv || !k || !pw)
        return 0;
    ok = drv->account_open(pw, k->sealed, FL_KQ_ACCOUNT_SEALED_LEN, sec) == FL_KQ_ACCOUNT_OK;
    drv->wipe(sec, sizeof sec); /* nothing stays open: opening is the password check */
    return ok;
}

static int kqi_change_password(const char *name, const char *pw)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    kqi_t *k = name ? kqi_find(name) : NULL;
    uint8_t fresh[FL_KQ_ACCOUNT_SEALED_LEN];

    if (!drv || !k || !pw || !pw[0] || seal_new(drv, pw, fresh) != 0)
        return -1;
    for (unsigned i = 0; i < sizeof fresh; i++)
        k->sealed[i] = fresh[i];
    return 0;
}

static void kqi_forget(const char *name)
{
    kqi_t *k = name ? kqi_find(name) : NULL;

    if (k) {
        volatile uint8_t *p = (volatile uint8_t *)k;
        for (unsigned i = 0; i < sizeof *k; i++)
            p[i] = 0;
    }
}

static const fl_fs_identity_kq_ops_t s_ops = {
    .enroll = kqi_enroll,
    .unlock = kqi_unlock,
    .switch_allowed = NULL, /* password-free switchuser unchanged: an open owner point */
    .change_password = kqi_change_password,
    .forget = kqi_forget,
};

void fl_fs_kq_bind_identity(void)
{
    (void)fl_fs_identity_set_kq(&s_ops);
}
