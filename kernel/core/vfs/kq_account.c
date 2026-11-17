/* Account unlock through the KeyQuorum driver, against the account store's facts. */
#include "fl/kq.h"

#include "contract_p5_server_catalog.h"
#include "fl/driver/keyquorum.h"
#include "fl/hex.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "server_shared_digest.h"
#include "session.h"
#include "user_db.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <sys/mman.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

fl_kq_requirement_t fl_kq_requirement_from_snapshot(const struct fl_user_record *current,
                                                   const struct fl_user_record *target, int sudo,
                                                   const struct fl_user_bridge *live, size_t nlive) {
    fl_kq_requirement_t out = {.factor = FL_KQ_ACCESS_REFUSED, .bridge_index = -1,
                               .needs_parent_grant = 0, .bridge_uid = ""};
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    const fl_user_record_t *cur = (const fl_user_record_t *)current;
    const fl_user_record_t *tgt = (const fl_user_record_t *)target;
    const fl_user_bridge_t *bridges_in = (const fl_user_bridge_t *)live;
    kq_bridge_t bridges[FL_USER_BRIDGES_MAX];
    const char *labels[FL_USER_BRIDGES_MAX][FL_USER_BRIDGE_MEMBERS_MAX];
    size_t i, j;
    int factor, idx = -1, grant = 0;

    if (!drv || !drv->access_requirement || !cur || !tgt)
        return out;
    if (nlive > FL_USER_BRIDGES_MAX || (nlive && !bridges_in))
        return out;
    for (i = 0; i < nlive; i++) {
        for (j = 0; j < bridges_in[i].nmembers && j < FL_USER_BRIDGE_MEMBERS_MAX; j++)
            labels[i][j] = bridges_in[i].members[j].label;
        bridges[i].uid = bridges_in[i].uid;
        bridges[i].members = labels[i];
        bridges[i].nmembers = bridges_in[i].nmembers;
    }
    /* parent_approval 0: no unlock_approval policy is stored on this side. */
    factor = drv->access_requirement(cur->key_id, tgt->key_id, sudo ? 1 : 0, nlive ? bridges : NULL,
                                     nlive, 0, &idx, &grant);
    switch (factor) {
    case FL_KQ_ACCESS_NONE:
    case FL_KQ_ACCESS_PASSWORD:
    case FL_KQ_ACCESS_KQST_PASSWORD:
        out.factor = factor;
        out.needs_parent_grant = grant ? 1 : 0;
        break;
    case FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST:
        if (idx >= 0 && (size_t)idx < nlive) {
            out.factor = factor;
            out.bridge_index = idx;
            out.needs_parent_grant = grant ? 1 : 0;
            memcpy(out.bridge_uid, bridges_in[idx].uid, strlen(bridges_in[idx].uid) + 1);
        }
        break;
    default:
        break; /* REFUSED, INVALID or a factor this build does not know: refused */
    }
    return out;
}

fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user,
                                             int sudo) {
    fl_kq_requirement_t out = {.factor = FL_KQ_ACCESS_REFUSED, .bridge_index = -1,
                               .needs_parent_grant = 0, .bridge_uid = ""};
    fl_user_record_t cur, tgt;
    fl_user_bridge_t *live;
    size_t n = 0;

    if (!current_user || !target_user)
        return out;
    if (fl_session_snapshot_account(current_user, &cur) != FL_RESULT_OK ||
        fl_session_snapshot_account(target_user, &tgt) != FL_RESULT_OK)
        return out;
    live = (fl_user_bridge_t *)calloc(FL_USER_BRIDGES_MAX, sizeof *live);
    if (!live)
        return out;
    /* Only what the store holds as live now; a store that cannot be read refuses. */
    if (fl_user_db_bridges_live(fl_session_user_db(), live, FL_USER_BRIDGES_MAX, &n) != FL_RESULT_OK)
        goto done;
    out = fl_kq_requirement_from_snapshot(&cur, &tgt, sudo, live, n);
done:
    free(live);
    return out;
}

/* The roster member at the other end from the presenter, else the presenter. */
static const char *bridge_verifier(const fl_user_bridge_t *b, const char *presenter) {
    size_t i;

    for (i = 0; i < b->nmembers; i++)
        if (strcmp(b->members[i].label, presenter) != 0)
            return b->members[i].label;
    return presenter;
}

enum {
    /* "FLBRFACTS1" + length prefixes + every field the digest binds. No struct padding. */
    KQ_FACTS_CAP = 10 + 2 + (FL_USER_BRIDGE_UID_MAX - 1) + 4 + FL_USER_BRIDGE_SALT_LEN + 32 + 1 + 4 +
                   FL_USER_BRIDGE_MEMBERS_MAX * (2 + (FL_HERITAGE_KEY_ID_MAX - 1) + 32 + 32) + 2 +
                   (FL_HERITAGE_KEY_ID_MAX - 1) + 4 + FL_USER_BRIDGE_SEALED_MAX + FL_USER_BRIDGE_SALT_LEN
};

static int facts_put(uint8_t *buf, size_t cap, size_t *used, const void *p, size_t n) {
    if (*used > cap || n > cap - *used)
        return -1;
    if (n)
        memcpy(buf + *used, p, n);
    *used += n;
    return 0;
}

static int facts_u16(uint8_t *buf, size_t cap, size_t *used, unsigned v) {
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};

    return facts_put(buf, cap, used, b, 2);
}

static int facts_u32(uint8_t *buf, size_t cap, size_t *used, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};

    return facts_put(buf, cap, used, b, 4);
}

/* Length of a NUL-terminated field that fits in cap bytes; -1 when it does not. */
static int facts_field(const char *s, size_t cap, size_t *len) {
    const char *end;

    if (!s)
        return -1;
    end = memchr(s, '\0', cap);
    if (!end)
        return -1;
    *len = (size_t)(end - s);
    return 0;
}

fl_result_t fl_kq_bridge_facts_hex(const struct fl_user_bridge *facts_in, const char *selected_label,
                                   const unsigned char *sealed, size_t sealed_len,
                                   const unsigned char wrap_salt[16], char facts_hex[65]) {
    const fl_user_bridge_t *facts = (const fl_user_bridge_t *)facts_in;
    uint8_t *canon = NULL;
    size_t used = 0, uid_len = 0, sel_len = 0, i;
    uint8_t destroyed;
    fl_result_t rc = FL_RESULT_INVAL;

    if (!facts_hex)
        return FL_RESULT_INVAL;
    mem_domain_secure_zero(facts_hex, 65);
    if (!facts || !selected_label || !sealed || !wrap_salt)
        return FL_RESULT_INVAL;
    if (facts->nmembers < 2 || facts->nmembers > FL_USER_BRIDGE_MEMBERS_MAX)
        return FL_RESULT_INVAL;
    if (sealed_len == 0 || sealed_len > FL_USER_BRIDGE_SEALED_MAX)
        return FL_RESULT_INVAL;
    if (facts_field(facts->uid, sizeof facts->uid, &uid_len) != 0 || uid_len == 0)
        return FL_RESULT_INVAL;
    if (facts_field(selected_label, FL_HERITAGE_KEY_ID_MAX, &sel_len) != 0 || sel_len == 0)
        return FL_RESULT_INVAL;
    canon = (uint8_t *)malloc(KQ_FACTS_CAP);
    if (!canon)
        return FL_RESULT_ERR;
    destroyed = facts->destroyed ? 1u : 0u;
    if (facts_put(canon, KQ_FACTS_CAP, &used, "FLBRFACTS1", 10) != 0 ||
        facts_u16(canon, KQ_FACTS_CAP, &used, (unsigned)uid_len) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, facts->uid, uid_len) != 0 ||
        facts_u32(canon, KQ_FACTS_CAP, &used, facts->generation) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, facts->salt, FL_USER_BRIDGE_SALT_LEN) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, facts->public_key, 32) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, &destroyed, 1) != 0 ||
        facts_u32(canon, KQ_FACTS_CAP, &used, (uint32_t)facts->nmembers) != 0)
        goto wipe;
    for (i = 0; i < facts->nmembers; i++) {
        size_t lab_len = 0;

        if (facts_field(facts->members[i].label, sizeof facts->members[i].label, &lab_len) != 0 ||
            lab_len == 0)
            goto wipe;
        if (facts_u16(canon, KQ_FACTS_CAP, &used, (unsigned)lab_len) != 0 ||
            facts_put(canon, KQ_FACTS_CAP, &used, facts->members[i].label, lab_len) != 0 ||
            facts_put(canon, KQ_FACTS_CAP, &used, facts->members[i].encryption_public, 32) != 0 ||
            facts_put(canon, KQ_FACTS_CAP, &used, facts->members[i].signing_public, 32) != 0)
            goto wipe;
    }
    if (facts_u16(canon, KQ_FACTS_CAP, &used, (unsigned)sel_len) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, selected_label, sel_len) != 0 ||
        facts_u32(canon, KQ_FACTS_CAP, &used, (uint32_t)sealed_len) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, sealed, sealed_len) != 0 ||
        facts_put(canon, KQ_FACTS_CAP, &used, wrap_salt, FL_USER_BRIDGE_SALT_LEN) != 0)
        goto wipe;
    if (fl_server_shared_sha256_hex(canon, used, facts_hex, FL_SERVER_CATALOG_HASH_HEX_MAX) ==
        FL_RESULT_OK)
        rc = FL_RESULT_OK;
    else
        mem_domain_secure_zero(facts_hex, 65);
wipe:
    mem_domain_secure_zero(canon, KQ_FACTS_CAP);
    free(canon);
    if (rc != FL_RESULT_OK)
        mem_domain_secure_zero(facts_hex, 65);
    return rc;
}

int fl_kq_prove_bridge_member(const char *uid, const char *member_user, const char *member_password,
                              char facts_hex[65]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    fl_user_record_t member;
    fl_user_bridge_t *facts = NULL;
    const char *labels[FL_USER_BRIDGE_MEMBERS_MAX];
    uint8_t pubs[FL_USER_BRIDGE_MEMBERS_MAX * 32];
    uint8_t sealed[FL_USER_BRIDGE_SEALED_MAX], wrap_salt[FL_USER_BRIDGE_SALT_LEN];
    uint8_t signing[32], encryption[32], challenge[32], artifact[1024];
    size_t slen = 0, alen = 0, i;
    int in_roster = 0;
    int st = FL_KQ_BRIDGE_INVALID;
    fl_result_t snap;

    if (facts_hex)
        mem_domain_secure_zero(facts_hex, 65);
    mem_domain_secure_zero(signing, sizeof signing);
    mem_domain_secure_zero(encryption, sizeof encryption);
    mem_domain_secure_zero(sealed, sizeof sealed);
    mem_domain_secure_zero(wrap_salt, sizeof wrap_salt);
    if (!drv || !drv->bridge_prove || !drv->bridge_verify || !uid || !uid[0] || !member_user ||
        !member_password)
        return FL_KQ_BRIDGE_INVALID;
    if (fl_session_snapshot_account(member_user, &member) != FL_RESULT_OK)
        return FL_KQ_BRIDGE_REFUSED;
    facts = (fl_user_bridge_t *)calloc(1, sizeof *facts);
    if (!facts)
        return FL_KQ_BRIDGE_INVALID;
    /* One snapshot of the facts and the sealed key, closed before the token is opened. */
    snap = fl_user_db_bridge_snapshot(fl_session_user_db(), uid, member.key_id, facts, sealed,
                                      sizeof sealed, &slen, wrap_salt);
    if (!facts->uid[0]) {
        st = FL_KQ_BRIDGE_STALE; /* no bridge, or the store could not be read */
        goto out;
    }
    for (i = 0; i < facts->nmembers && i < FL_USER_BRIDGE_MEMBERS_MAX; i++) {
        labels[i] = facts->members[i].label;
        memcpy(pubs + i * 32, facts->members[i].signing_public, 32);
        if (strcmp(facts->members[i].label, member.key_id) == 0)
            in_roster = 1;
    }
    if (facts->destroyed) {
        st = FL_KQ_BRIDGE_STALE;
        goto out;
    }
    if (!in_roster || snap != FL_RESULT_OK) {
        st = FL_KQ_BRIDGE_REFUSED;
        goto out;
    }
    if (fl_kq_open_account_keys(member.name, member_password, NULL, signing, encryption) !=
        FL_KQ_ACCOUNT_OK) {
        st = FL_KQ_BRIDGE_REFUSED;
        goto out;
    }
    if (fl_random_bytes(challenge, sizeof challenge) != 0) {
        st = FL_KQ_BRIDGE_INVALID;
        goto wipe;
    }
    st = drv->bridge_prove(facts->uid, facts->generation, facts->salt, facts->public_key,
                           member.key_id, sealed, slen, wrap_salt, encryption, signing, challenge,
                           sizeof challenge, artifact, sizeof artifact, &alen);
    if (st == FL_KQ_BRIDGE_OK)
        st = drv->bridge_verify(facts->uid, facts->generation, facts->salt, facts->public_key,
                                facts->destroyed, labels, pubs, facts->nmembers,
                                bridge_verifier(facts, member.key_id), challenge, sizeof challenge,
                                artifact, alen);
    else if (st == FL_KQ_BRIDGE_NO_ROOM)
        st = FL_KQ_BRIDGE_INVALID;
    if (st == FL_KQ_BRIDGE_OK && facts_hex &&
        fl_kq_bridge_facts_hex(facts, member.key_id, sealed, slen, wrap_salt, facts_hex) !=
            FL_RESULT_OK)
        st = FL_KQ_BRIDGE_INVALID;
wipe:
    mem_domain_secure_zero(signing, sizeof signing);
    mem_domain_secure_zero(encryption, sizeof encryption);
out:
    mem_domain_secure_zero(artifact, sizeof artifact);
    mem_domain_secure_zero(sealed, sizeof sealed);
    mem_domain_secure_zero(wrap_salt, sizeof wrap_salt);
    free(facts);
    if (st != FL_KQ_BRIDGE_OK && st != FL_KQ_BRIDGE_STALE && st != FL_KQ_BRIDGE_INVALID)
        st = FL_KQ_BRIDGE_REFUSED;
    if (st != FL_KQ_BRIDGE_OK && facts_hex)
        mem_domain_secure_zero(facts_hex, 65);
    return st;
}

/* Opens a token file; both secrets are zeroed unless OK (caller wipes). */
static int open_token_file(const char *label, const char *password, const char *path,
                           const unsigned char device_id[16], const unsigned char signing_pub[32],
                           const unsigned char encryption_pub[32], uint8_t signing_secret_out[32],
                           uint8_t encryption_secret_out[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    uint8_t *token = NULL;
    size_t tlen = 0;
    FILE *fp;
    long flen;
    int st;

    mem_domain_secure_zero(signing_secret_out, 32);
    mem_domain_secure_zero(encryption_secret_out, 32);
    if (!drv || !drv->slot_open || !path || !path[0])
        return FL_KQ_ACCOUNT_INVALID;
    fp = fopen(path, "rb");
    if (!fp)
        return FL_KQ_ACCOUNT_INVALID;
    if (fseek(fp, 0, SEEK_END) != 0 || (flen = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return FL_KQ_ACCOUNT_INVALID;
    }
    tlen = (size_t)flen;
    if (tlen == 0 || tlen > 65536u) {
        fclose(fp);
        return FL_KQ_ACCOUNT_INVALID;
    }
    token = (uint8_t *)malloc(tlen);
    if (!token || fread(token, 1, tlen, fp) != tlen) {
        free(token);
        fclose(fp);
        return FL_KQ_ACCOUNT_INVALID;
    }
    fclose(fp);
    st = drv->slot_open(token, tlen, label, password, device_id, signing_pub, encryption_pub,
                        signing_secret_out, encryption_secret_out);
    mem_domain_secure_zero(token, tlen);
    free(token);
    if (st != FL_KQ_ACCOUNT_OK) {
        mem_domain_secure_zero(signing_secret_out, 32);
        mem_domain_secure_zero(encryption_secret_out, 32);
    }
    return st;
}

int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]) {
    const fl_user_db_t *db;
    fl_user_record_t snapshot;
    char path[512];
    unsigned char device_id[16], signing_pub[32], encryption_pub[32];

    if (!user || !user[0] || !password || !signing_secret_out || !encryption_secret_out)
        return FL_KQ_ACCOUNT_INVALID;
    mem_domain_secure_zero(signing_secret_out, 32);
    mem_domain_secure_zero(encryption_secret_out, 32);
    if (fl_session_snapshot_account(user, &snapshot) != FL_RESULT_OK)
        return FL_KQ_ACCOUNT_INVALID;
    db = fl_session_user_db();
    if (!db)
        return FL_KQ_ACCOUNT_INVALID;
    if (kqst_path_or_null && kqst_path_or_null[0]) {
        if (strlen(kqst_path_or_null) >= sizeof path)
            return FL_KQ_ACCOUNT_INVALID;
        memcpy(path, kqst_path_or_null, strlen(kqst_path_or_null) + 1);
    } else if (fl_user_db_kqst_path(db, snapshot.key_id, path, sizeof path) != FL_RESULT_OK) {
        return FL_KQ_ACCOUNT_INVALID;
    }
    if (fl_user_db_kqst_pairing(db, snapshot.key_id, device_id, signing_pub, encryption_pub) !=
        FL_RESULT_OK)
        return FL_KQ_ACCOUNT_INVALID;
    return open_token_file(snapshot.key_id, password, path, device_id, signing_pub, encryption_pub,
                           signing_secret_out, encryption_secret_out);
}

int fl_kq_check_token(const char *key_id, const char *password, const char *kqst_path,
                      const unsigned char device_id[16], const unsigned char signing_public[32],
                      const unsigned char encryption_public[32]) {
    uint8_t signing[32], encryption[32];
    int st;

    if (!key_id || !key_id[0] || !password || !kqst_path || !device_id || !signing_public ||
        !encryption_public)
        return FL_KQ_ACCOUNT_INVALID;
    st = open_token_file(key_id, password, kqst_path, device_id, signing_public, encryption_public,
                         signing, encryption);
    mem_domain_secure_zero(signing, sizeof signing);
    mem_domain_secure_zero(encryption, sizeof encryption);
    return st;
}

int fl_kq_open_account_secrets(const char *user, const char *password, const char *kqst_path_or_null,
                               uint8_t signing_secret_out[32]) {
    uint8_t encryption_secret[32];
    int st;

    if (!signing_secret_out)
        return FL_KQ_ACCOUNT_INVALID;
    st = fl_kq_open_account_keys(user, password, kqst_path_or_null, signing_secret_out,
                                 encryption_secret);
    mem_domain_secure_zero(encryption_secret, sizeof encryption_secret);
    return st;
}

int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null) {
    uint8_t signing_secret[32];
    int st = fl_kq_open_account_secrets(user, password, kqst_path_or_null, signing_secret);

    mem_domain_secure_zero(signing_secret, sizeof signing_secret);
    return st;
}

/* ---- Kernel-made tokens: "<store dir>/kqst/<key id>.kqst", owner-only, sealing the account's keys ---- */
/* Letters, digits, '.', '_', '-'; no leading '.' and no "..": the name cannot leave the token directory. */
static int token_name_safe(const char *key_id) {
    size_t i;

    if (!key_id || !key_id[0] || key_id[0] == '.' || strstr(key_id, ".."))
        return 0;
    for (i = 0; key_id[i]; i++) {
        unsigned char c = (unsigned char)key_id[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '_' || c == '-'))
            return 0;
        if (i >= FL_HERITAGE_KEY_ID_MAX)
            return 0;
    }
    return 1;
}

int fl_kq_account_token_path(const char *db_path, const char *key_id, char *out, size_t cap) {
    const char *slash;
    size_t dir_len;
    int n;

    if (!db_path || !out || cap == 0 || !token_name_safe(key_id))
        return -1;
    slash = strrchr(db_path, '/');
    dir_len = slash ? (size_t)(slash - db_path) : 0;
    if (slash && dir_len == 0)
        n = snprintf(out, cap, "/kqst/%s.kqst", key_id);
    else if (slash)
        n = snprintf(out, cap, "%.*s/kqst/%s.kqst", (int)dir_len, db_path, key_id);
    else
        n = snprintf(out, cap, "kqst/%s.kqst", key_id);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

/* Stages a random .new name (owner-only, never over a file), then renames. The directory is held. */
static int write_token_file(const char *path, const uint8_t *bytes, size_t len) {
    char dir[FL_KQ_TOKEN_PATH_MAX];
    char name[FL_KQ_TOKEN_PATH_MAX], tmp_name[FL_KQ_TOKEN_PATH_MAX + 24], tag_hex[17];
    unsigned char tag[8];
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    size_t off = 0, blen = strlen(base);
    int fd, dfd = AT_FDCWD;
    struct stat st;

    if (blen <= 5 || strcmp(base + blen - 5, ".kqst") != 0 || blen - 5 >= sizeof name)
        return -1;
    memcpy(name, base, blen - 5);
    name[blen - 5] = '\0';
    if (!token_name_safe(name))
        return -1;
    snprintf(name, sizeof name, "%s", base);
    /* Each write stages under its own name, so two writers never share a staged file. */
    if (fl_random_bytes(tag, sizeof tag) != 0)
        return -1;
    fl_hex_encode(tag, sizeof tag, tag_hex);
    if (snprintf(tmp_name, sizeof tmp_name, "%s.%s.new", name, tag_hex) >= (int)sizeof tmp_name)
        return -1;
    if (slash) {
        size_t dlen = (size_t)(slash - path);

        if (dlen >= sizeof dir)
            return -1;
        memcpy(dir, path, dlen);
        dir[dlen] = '\0';
        if (dlen && mkdir(dir, 0700) != 0 && errno != EEXIST)
            return -1;
        dfd = open(dlen ? dir : "/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (dfd < 0)
            return -1;
        if (fstat(dfd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            close(dfd);
            return -1;
        }
    }
    fd = openat(dfd, tmp_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        goto fail_dir;
    while (off < len) {
        ssize_t w = write(fd, bytes + off, len - off);

        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0) {
            close(fd);
            goto fail_tmp;
        }
        off += (size_t)w;
    }
    if (fsync(fd) != 0) {
        close(fd);
        goto fail_tmp;
    }
    if (close(fd) != 0)
        goto fail_tmp;
    if (renameat(dfd, tmp_name, dfd, name) != 0)
        goto fail_tmp;
    if (dfd != AT_FDCWD)
        close(dfd);
    return 0;
fail_tmp:
    (void)unlinkat(dfd, tmp_name, 0);
fail_dir:
    if (dfd != AT_FDCWD)
        close(dfd);
    return -1;
}

int fl_kq_write_account_token(const char *key_id, const char *password,
                              const uint8_t signing_secret[32], const uint8_t box_secret[32],
                              const unsigned char device_id[16], const char *path,
                              unsigned char signing_public_out[32],
                              unsigned char encryption_public_out[32]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    uint8_t token[FL_KQ_SLOT_TOKEN_LEN(FL_HERITAGE_KEY_ID_MAX)];
    size_t tlen = 0;
    int st;

    if (!drv || !drv->slot_seal)
        return FL_KQ_ACCOUNT_INVALID;
    if (!key_id || !password || !password[0] || !signing_secret || !box_secret || !device_id ||
        !path || !path[0] || strlen(path) >= FL_KQ_TOKEN_PATH_MAX || !signing_public_out ||
        !encryption_public_out)
        return FL_KQ_ACCOUNT_INVALID;
    st = drv->slot_seal(key_id, password, device_id, signing_secret, box_secret, token, sizeof token,
                        &tlen, signing_public_out, encryption_public_out);
    if (st == FL_KQ_ACCOUNT_OK && (tlen == 0 || tlen > sizeof token))
        st = FL_KQ_ACCOUNT_INVALID;
    if (st == FL_KQ_ACCOUNT_OK && write_token_file(path, token, tlen) != 0)
        st = FL_KQ_ACCOUNT_INVALID;
    mem_domain_secure_zero(token, sizeof token);
    return st;
}

/* Opens a co-signer secret; zeroed unless OK (caller wipes). */
static int open_cosigner(const char *name, const char *password, int kqst, uint8_t secret_out[32],
                         unsigned char public_out[32], char key_id_out[FL_HERITAGE_KEY_ID_MAX]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    const fl_user_db_t *db = fl_session_user_db();
    fl_user_record_t u;
    unsigned char dev[16], enc_pub[32];
    uint8_t box[32];
    int st;

    mem_domain_secure_zero(secret_out, 32);
    if (!drv || !db || !name || !password || fl_session_snapshot_account(name, &u) != FL_RESULT_OK ||
        !u.key_id[0])
        return FL_KQ_ACCOUNT_INVALID;
    snprintf(key_id_out, FL_HERITAGE_KEY_ID_MAX, "%s", u.key_id);
    if (kqst) {
        if (fl_user_db_kqst_pairing(db, u.key_id, dev, public_out, enc_pub) != FL_RESULT_OK)
            return FL_KQ_ACCOUNT_INVALID;
        st = fl_kq_open_account_keys(name, password, NULL, secret_out, box);
        mem_domain_secure_zero(box, sizeof box);
        mem_domain_secure_zero(&u, sizeof u);
        return st;
    }
    if (!u.has_key || !drv->account_open) {
        mem_domain_secure_zero(&u, sizeof u);
        return FL_KQ_ACCOUNT_INVALID;
    }
    memcpy(public_out, u.signing_public, 32);
    st = drv->account_open(password, u.signing_sealed, sizeof u.signing_sealed, secret_out);
    mem_domain_secure_zero(&u, sizeof u);
    if (st != FL_KQ_ACCOUNT_OK)
        mem_domain_secure_zero(secret_out, 32);
    return st;
}

static int cosigner_public(const char *name, int kqst, unsigned char public_out[32],
                           char key_id_out[FL_HERITAGE_KEY_ID_MAX]) {
    const fl_user_db_t *db = fl_session_user_db();
    fl_user_record_t u;
    unsigned char dev[16], enc_pub[32];
    int st = FL_KQ_ACCOUNT_INVALID;

    if (!db || !name || fl_session_snapshot_account(name, &u) != FL_RESULT_OK || !u.key_id[0])
        return FL_KQ_ACCOUNT_INVALID;
    snprintf(key_id_out, FL_HERITAGE_KEY_ID_MAX, "%s", u.key_id);
    if (kqst ? u.auth_mode == FL_USER_AUTH_PASSWORD_KQST &&
                   fl_user_db_kqst_pairing(db, u.key_id, dev, public_out, enc_pub) == FL_RESULT_OK
             : u.has_key && u.auth_mode != FL_USER_AUTH_PASSWORD_KQST) {
        if (!kqst)
            memcpy(public_out, u.signing_public, 32);
        st = FL_KQ_ACCOUNT_OK;
    }
    mem_domain_secure_zero(&u, sizeof u);
    return st;
}

/* Both sign the action under a fresh nonce and both must verify; SIGN zeroes the signatures. */
static int cosign_with(const char *inv_key, const char *root_key, const char *action,
                       const uint8_t inv_secret[32], const uint8_t root_secret[32],
                       const unsigned char inv_pub[32], const unsigned char root_pub[32],
                       uint8_t nonce_out[16], uint8_t invoker_sig_out[64], uint8_t root_sig_out[64]) {
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    size_t alen;

    if (!nonce_out || !invoker_sig_out || !root_sig_out || !action)
        return FL_KQ_COSIGN_SIGN;
    mem_domain_zero(invoker_sig_out, 64);
    mem_domain_zero(root_sig_out, 64);
    alen = strlen(action);
    if (!drv || !drv->sudo_sign || !drv->sudo_verify || alen == 0 || alen > FL_KQ_SUDO_ACTION_MAX)
        return FL_KQ_COSIGN_SIGN;
    if (fl_random_bytes(nonce_out, 16) == 0 &&
        drv->sudo_sign(FL_KQ_SUDO_INVOKER, inv_key, root_key, (const uint8_t *)action, alen, nonce_out,
                       inv_secret, invoker_sig_out) == FL_KQ_ACCOUNT_OK &&
        drv->sudo_sign(FL_KQ_SUDO_AUTHORITY, inv_key, root_key, (const uint8_t *)action, alen, nonce_out,
                       root_secret, root_sig_out) == FL_KQ_ACCOUNT_OK &&
        /* Verified under the store's public keys, never the ones just used. */
        drv->sudo_verify(inv_key, root_key, (const uint8_t *)action, alen, nonce_out, inv_pub,
                         invoker_sig_out, root_pub, root_sig_out) == 1)
        return FL_KQ_COSIGN_OK;
    mem_domain_zero(invoker_sig_out, 64);
    mem_domain_zero(root_sig_out, 64);
    return FL_KQ_COSIGN_SIGN;
}

/* The sudo window's two secrets: kept out of swap and core dumps, wiped when it ends. */
typedef struct {
    int           used;
    int           inv_kqst;
    int64_t       expires_ns;
    char          inv_name[FL_USER_NAME_MAX];
    char          root_name[FL_USER_NAME_MAX];
    uint8_t       inv_secret[32];
    uint8_t       root_secret[32];
} kq_sudo_hold_t;

static kq_sudo_hold_t s_hold __attribute__((aligned(4096)));
static pthread_mutex_t s_hold_mu = PTHREAD_MUTEX_INITIALIZER;
static int s_hold_locked;

int fl_kq_lock_pages(void *p, size_t len) {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    int locked;

#if defined(__linux__) && defined(SYS_mlock)
    locked = syscall(SYS_mlock, p, len) == 0;
#else
    locked = mlock(p, len) == 0;
#endif
#if defined(__linux__) && defined(MADV_DONTDUMP)
    {
        long page = sysconf(_SC_PAGESIZE);

        if (page > 0)
            len = (len + (size_t)page - 1) & ~((size_t)page - 1);
        (void)madvise(p, len, MADV_DONTDUMP);
    }
#endif
    return locked;
#else
    (void)p;
    (void)len;
    return 0;
#endif
}

/* s_hold_mu is held. */
static void hold_lock_page_locked(void) {
    if (!s_hold_locked)
        s_hold_locked = fl_kq_lock_pages(&s_hold, sizeof s_hold);
}

/* s_hold_mu is held. */
static void hold_wipe_locked(void) {
    mem_domain_secure_zero(&s_hold, sizeof s_hold);
}

int fl_kq_sudo_hold_open(const char *invoker_name, const char *invoker_password, int invoker_kqst,
                         const char *root_name, const char *root_password, const char *action,
                         int64_t expires_ns, uint8_t nonce_out[16], uint8_t invoker_sig_out[64],
                         uint8_t root_sig_out[64]) {
    char inv_key[FL_HERITAGE_KEY_ID_MAX], root_key[FL_HERITAGE_KEY_ID_MAX];
    uint8_t inv_secret[32], root_secret[32];
    unsigned char inv_pub[32], root_pub[32];
    int rc;

    if (!nonce_out || !invoker_sig_out || !root_sig_out || !action || !invoker_name || !root_name)
        return FL_KQ_COSIGN_SIGN;
    mem_domain_zero(invoker_sig_out, 64);
    mem_domain_zero(root_sig_out, 64);
    if (open_cosigner(root_name, root_password, 1, root_secret, root_pub, root_key) != FL_KQ_ACCOUNT_OK) {
        mem_domain_secure_zero(root_secret, sizeof root_secret);
        return FL_KQ_COSIGN_AUTHORITY;
    }
    if (open_cosigner(invoker_name, invoker_password, invoker_kqst, inv_secret, inv_pub, inv_key) !=
        FL_KQ_ACCOUNT_OK) {
        rc = FL_KQ_COSIGN_INVOKER;
        goto wipe;
    }
    rc = cosign_with(inv_key, root_key, action, inv_secret, root_secret, inv_pub, root_pub, nonce_out,
                     invoker_sig_out, root_sig_out);
    if (rc == FL_KQ_COSIGN_OK && expires_ns > 0) {
        pthread_mutex_lock(&s_hold_mu);
        hold_lock_page_locked();
        hold_wipe_locked(); /* a window that is replaced is wiped first */
        s_hold.used = 1;
        s_hold.inv_kqst = invoker_kqst ? 1 : 0;
        s_hold.expires_ns = expires_ns;
        snprintf(s_hold.inv_name, sizeof s_hold.inv_name, "%s", invoker_name);
        snprintf(s_hold.root_name, sizeof s_hold.root_name, "%s", root_name);
        mem_domain_copy(s_hold.inv_secret, inv_secret, sizeof s_hold.inv_secret);
        mem_domain_copy(s_hold.root_secret, root_secret, sizeof s_hold.root_secret);
        pthread_mutex_unlock(&s_hold_mu);
    }
wipe:
    mem_domain_secure_zero(inv_secret, sizeof inv_secret);
    mem_domain_secure_zero(root_secret, sizeof root_secret);
    return rc;
}

int fl_kq_sudo_hold_cosign(const char *invoker_name, const char *action, int64_t now_ns,
                           uint8_t nonce_out[16], uint8_t invoker_sig_out[64], uint8_t root_sig_out[64]) {
    char inv_name[FL_USER_NAME_MAX], root_name[FL_USER_NAME_MAX];
    char inv_key[FL_HERITAGE_KEY_ID_MAX], root_key[FL_HERITAGE_KEY_ID_MAX];
    uint8_t inv_secret[32], root_secret[32];
    unsigned char inv_pub[32], root_pub[32];
    int inv_kqst, rc;

    if (!nonce_out || !invoker_sig_out || !root_sig_out || !action || !invoker_name)
        return FL_KQ_COSIGN_SIGN;
    mem_domain_zero(invoker_sig_out, 64);
    mem_domain_zero(root_sig_out, 64);
    pthread_mutex_lock(&s_hold_mu);
    if (!s_hold.used || s_hold.expires_ns <= now_ns || strcmp(s_hold.inv_name, invoker_name) != 0) {
        if (s_hold.used && s_hold.expires_ns <= now_ns)
            hold_wipe_locked();
        pthread_mutex_unlock(&s_hold_mu);
        return FL_KQ_COSIGN_AUTHORITY; /* Root's key is not held for this account now */
    }
    inv_kqst = s_hold.inv_kqst;
    snprintf(inv_name, sizeof inv_name, "%s", s_hold.inv_name);
    snprintf(root_name, sizeof root_name, "%s", s_hold.root_name);
    mem_domain_copy(inv_secret, s_hold.inv_secret, sizeof inv_secret);
    mem_domain_copy(root_secret, s_hold.root_secret, sizeof root_secret);
    pthread_mutex_unlock(&s_hold_mu);

    /* The store's keys now: a token re-sealed or re-paired since the sudo no longer counts. */
    if (cosigner_public(root_name, 1, root_pub, root_key) != FL_KQ_ACCOUNT_OK)
        rc = FL_KQ_COSIGN_AUTHORITY;
    else if (cosigner_public(inv_name, inv_kqst, inv_pub, inv_key) != FL_KQ_ACCOUNT_OK)
        rc = FL_KQ_COSIGN_INVOKER;
    else
        rc = cosign_with(inv_key, root_key, action, inv_secret, root_secret, inv_pub, root_pub, nonce_out,
                         invoker_sig_out, root_sig_out);
    mem_domain_secure_zero(inv_secret, sizeof inv_secret);
    mem_domain_secure_zero(root_secret, sizeof root_secret);
    return rc;
}

void fl_kq_sudo_hold_extend(int64_t expires_ns) {
    pthread_mutex_lock(&s_hold_mu);
    if (s_hold.used)
        s_hold.expires_ns = expires_ns;
    pthread_mutex_unlock(&s_hold_mu);
}

void fl_kq_sudo_hold_release(void) {
    pthread_mutex_lock(&s_hold_mu);
    hold_wipe_locked();
    pthread_mutex_unlock(&s_hold_mu);
}

int fl_kq_sudo_hold_expire(int64_t now_ns) {
    int wiped = 0;

    pthread_mutex_lock(&s_hold_mu);
    if (s_hold.used && s_hold.expires_ns <= now_ns) {
        hold_wipe_locked();
        wiped = 1;
    }
    pthread_mutex_unlock(&s_hold_mu);
    return wiped;
}

int fl_kq_sudo_hold_active(int64_t now_ns) {
    int active;

    pthread_mutex_lock(&s_hold_mu);
    active = s_hold.used && s_hold.expires_ns > now_ns;
    pthread_mutex_unlock(&s_hold_mu);
    return active;
}

