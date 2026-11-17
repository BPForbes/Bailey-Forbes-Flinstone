/* KeyQuorum file histories in the freestanding image (FL-02 stage 2). A ramfs file is copied into the
 * in-memory storage table under "/kq<ramfs path>" and the checkpoints keep its .kqtf beside it there;
 * every revision is signed by KeyQuorum with the signing key kq_identity.c sealed for the account, opened
 * with its password for that one signature. Trust is KeyQuorum's (the file's own policy over the keys
 * kq_identity.c holds); nothing here decides it. */
#include "kq_files.h"

#include "identity.h"
#include "ramfs.h"
#include "fl/driver/keyquorum.h"
#include "kq_checkpoint.h"
#include "kq_mem_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KQ_PREFIX "/kq"

static void (*s_emit)(const char *line);

void fl_fs_kq_files_init(void (*emit)(const char *line))
{
    s_emit = emit;
}

static void say(const char *a, const char *b, const char *c)
{
    char line[256];

    if (!s_emit)
        return;
    snprintf(line, sizeof line, "%s%s%s\r\n", a, b ? b : "", c ? c : "");
    s_emit(line);
}

/* "/kq" plus the file's absolute ramfs path; a ".." part is refused rather than resolved twice. */
static int store_path(int session, const char *path, char *out, size_t cap)
{
    char cwd[FL_FS_RAMFS_PATH];
    const char *p;
    int n;

    if (!path || !path[0])
        return -1;
    for (p = path; *p; p++)
        if (p[0] == '.' && p[1] == '.' && (p == path || p[-1] == '/') && (p[2] == '/' || !p[2]))
            return -1;
    if (path[0] == '/') {
        n = snprintf(out, cap, KQ_PREFIX "%s", path);
    } else {
        fl_fs_ramfs_pwd(session, cwd, sizeof cwd);
        n = snprintf(out, cap, KQ_PREFIX "%s%s%s", cwd, cwd[0] && cwd[strlen(cwd) - 1] == '/' ? "" : "/", path);
    }
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static void rev_hex(const uint8_t rev[32], char out[17])
{
    static const char digits[] = "0123456789abcdef";

    for (int i = 0; i < 8; i++) {
        out[2 * i] = digits[rev[i] >> 4];
        out[2 * i + 1] = digits[rev[i] & 15];
    }
    out[16] = '\0';
}

/* The ramfs text as the store's copy of the file, which the checkpoints read. */
static int copy_in(int session, const char *path, const char *kpath)
{
    const fl_kq_storage_t *st = fl_kq_cp_storage();
    char text[FL_FS_RAMFS_DATA];

    if (fl_fs_ramfs_cat(session, path, text, sizeof text) != 0)
        return -1;
    return st->write_atomic(st->ctx, kpath, (const uint8_t *)text, strlen(text));
}

int fl_fs_kq_file_sign(int session, const char *path, const char *password)
{
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const fl_kq_keydir_t *keys = fl_fs_kq_keydir();
    const char *label = keys->resolve(keys, fl_fs_identity_user(session));
    char kpath[FL_KQ_MEM_PATH_MAX], at[25], hex[17];
    uint8_t sec[32], rev[32];
    fl_kq_cp_result_t cp;

    if (!drv || !label) {
        say("server file sign: this account has no KeyQuorum key", NULL, NULL);
        return 1;
    }
    if (store_path(session, path, kpath, sizeof kpath) != 0 || copy_in(session, path, kpath) != 0) {
        say("server file sign: cannot read ", path, NULL);
        return 1;
    }
    if (fl_fs_utc_text(at) != 0) {
        say("server file sign: no clock", NULL, NULL);
        return 1;
    }
    if (fl_fs_kq_open_signing(label, password, sec) != 0) {
        say("authentication failed", NULL, NULL);
        return 1;
    }
    cp = fl_kq_cp_track_with(keys, kpath, label, sec, at, rev);
    if (cp == FL_KQ_CP_EXISTS)
        cp = fl_kq_cp_sign_head_with(kpath, label, sec, at);
    drv->wipe(sec, sizeof sec);
    if (cp == FL_KQ_CP_OK && fl_kq_cp_head(kpath, rev) == FL_KQ_CP_OK) {
        rev_hex(rev, hex);
        say("server file sign: signed by ", label, NULL);
        say("revision ", hex, NULL);
        return 0;
    }
    say("server file sign: ",
        cp == FL_KQ_CP_UNTRUSTED ? "the newest revision is another account's" : fl_kq_cp_reason(cp), NULL);
    return 1;
}

static int say_event(const char *line, size_t n, void *ctx)
{
    char text[200];

    (void)ctx;
    fl_kq_cp_format_event(line, n, text, sizeof text);
    say(text, NULL, NULL);
    return 0;
}

int fl_fs_kq_file_log(int session, const char *path)
{
    char kpath[FL_KQ_MEM_PATH_MAX];
    fl_kq_buf_t lines = {0};
    fl_kq_cp_result_t cp;

    if (store_path(session, path, kpath, sizeof kpath) != 0) {
        say("server file log: not a plain path", NULL, NULL);
        return 1;
    }
    cp = fl_kq_cp_history(kpath, &lines);
    if (cp == FL_KQ_CP_IO)
        say("no history for ", path, " (server file sign starts one)");
    else if (cp != FL_KQ_CP_OK)
        say("server file log: ", fl_kq_cp_reason(cp), NULL);
    else if (fl_kq_cp_for_each_event(&lines, say_event, NULL) == 0u)
        say("no history for ", path, NULL);
    fl_kq_buf_free(&lines);
    return cp == FL_KQ_CP_OK ? 0 : 1;
}

int fl_fs_kq_file_verify(int session, const char *path)
{
    char kpath[FL_KQ_MEM_PATH_MAX], author[64], hex[17];
    uint8_t rev[32];
    fl_kq_cp_result_t cp;

    if (store_path(session, path, kpath, sizeof kpath) != 0) {
        say("server file verify: not a plain path", NULL, NULL);
        return 1;
    }
    cp = fl_kq_cp_head_trusted_with(fl_fs_kq_keydir(), kpath, rev);
    if (cp == FL_KQ_CP_OK && fl_kq_cp_head_author(kpath, author, sizeof author) == FL_KQ_CP_OK) {
        rev_hex(rev, hex);
        say("server file verify: trusted, revision ", hex, NULL);
        say("signed by ", author, NULL);
        return 0;
    }
    if (cp == FL_KQ_CP_IO)
        say("no history for ", path, " (server file sign starts one)");
    else
        say("server file verify: ", cp == FL_KQ_CP_UNTRUSTED ? "the newest revision is not trusted" : fl_kq_cp_reason(cp),
            NULL);
    return 1;
}

/* The boot self-test's own key directory: one throwaway key under one label. */
typedef struct {
    fl_kq_keydir_t base;
    uint8_t pub[32];
} selftest_dir_t;

static const char *st_resolve(const fl_kq_keydir_t *dir, const char *who)
{
    (void)dir;
    return who && strcmp(who, "SelfTest") == 0 ? "SelfTest" : NULL;
}

static const unsigned char *st_signing(const fl_kq_keydir_t *dir, const char *label)
{
    return label && strcmp(label, "SelfTest") == 0 ? ((const selftest_dir_t *)dir)->pub : NULL;
}

static const unsigned char *st_box(const fl_kq_keydir_t *dir, const char *label)
{
    (void)dir, (void)label;
    return NULL;
}

int fl_fs_kq_selftest(void)
{
    static const char path[] = KQ_PREFIX "/.selftest";
    const fl_kq_driver_t *drv = fl_kq_driver_get();
    const fl_kq_storage_t *st = fl_kq_cp_storage();
    selftest_dir_t dir = { { st_resolve, st_signing, st_box }, { 0 } };
    uint8_t sec[32], other[32], other_pub[32], rev[32], head[32];
    fl_kq_buf_t lines = {0};
    char at[25];
    int ok = 0;

    if (!drv || fl_fs_utc_text(at) != 0 || drv->account_keygen(sec, dir.pub) != FL_KQ_ACCOUNT_OK)
        return -1;
    if (drv->account_keygen(other, other_pub) == FL_KQ_ACCOUNT_OK &&
        st->write_atomic(st->ctx, path, (const uint8_t *)"one\n", 4) == 0 &&
        fl_kq_cp_track_with(&dir.base, path, "SelfTest", sec, at, rev) == FL_KQ_CP_OK &&
        fl_kq_cp_head_trusted_with(&dir.base, path, head) == FL_KQ_CP_OK && drv->equal(head, rev, 32) &&
        st->write_atomic(st->ctx, path, (const uint8_t *)"two\n", 4) == 0 &&
        fl_kq_cp_sign_head_with(path, "SelfTest", sec, at) == FL_KQ_CP_OK &&
        fl_kq_cp_head_trusted_with(&dir.base, path, head) == FL_KQ_CP_OK && !drv->equal(head, rev, 32) &&
        fl_kq_cp_history(path, &lines) == FL_KQ_CP_OK && lines.len > 0 &&
        /* A revision signed with a key the directory does not hold for the label is not trusted. */
        fl_kq_cp_sign_head_with(path, "SelfTest", other, at) == FL_KQ_CP_OK &&
        fl_kq_cp_head_trusted_with(&dir.base, path, head) == FL_KQ_CP_UNTRUSTED)
        ok = 1;
    drv->wipe(sec, sizeof sec);
    drv->wipe(other, sizeof other);
    fl_kq_buf_free(&lines);
    fl_kq_storage_memory_reset();
    return ok ? 0 : -1;
}
