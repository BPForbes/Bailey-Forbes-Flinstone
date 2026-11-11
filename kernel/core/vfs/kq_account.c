/*
 * Account unlock helpers (FL-15, FL-12): open a KQST through the KeyQuorum driver using pairing
 * facts from the account store, ask KeyQuorum which factor a switch demands (the live bridges are
 * the store's), and prove bridge membership against the store's current bridge facts. Box-key
 * ownership after open remains an owner decision; callers that need the signing secret online must
 * publish it themselves (key service hook).
 */
#include "fl/kq.h"

#include "contract_p5_server_catalog.h"
#include "fl/driver/keyquorum.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "server_shared_digest.h"
#include "session.h"
#include "user_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* A verifier for the proof: the roster member at the other end from the presenter, else the
 * presenter (kq_bridge_verify requires a member). */
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

/* Length of a NUL-terminated field that fits in cap bytes, including the NUL. -1 when it does not. */
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

/**
 * Open the account's KQST. On OK, both secrets are written (caller wipes); otherwise both are zeroed.
 */
int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]) {
    const fl_kq_driver_t *drv;
    const fl_user_db_t *db;
    fl_user_record_t snapshot;
    char path[512];
    unsigned char device_id[16], signing_pub[32], encryption_pub[32];
    uint8_t *token = NULL;
    size_t tlen = 0;
    FILE *fp;
    long flen;
    int st;

    if (!user || !user[0] || !password || !signing_secret_out || !encryption_secret_out)
        return FL_KQ_ACCOUNT_INVALID;
    mem_domain_secure_zero(signing_secret_out, 32);
    mem_domain_secure_zero(encryption_secret_out, 32);
    drv = fl_kq_driver_get_or_register();
    if (!drv || !drv->slot_open)
        return FL_KQ_ACCOUNT_INVALID;
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
    st = drv->slot_open(token, tlen, snapshot.key_id, password, device_id, signing_pub,
                        encryption_pub, signing_secret_out, encryption_secret_out);
    mem_domain_secure_zero(token, tlen);
    free(token);
    if (st != FL_KQ_ACCOUNT_OK) {
        mem_domain_secure_zero(signing_secret_out, 32);
        mem_domain_secure_zero(encryption_secret_out, 32);
    }
    return st;
}

/**
 * Open the account's KQST. On OK, signing_secret_out holds the signing secret (caller wipes);
 * the encryption secret is wiped here (box-key ownership is not decided on this train).
 */
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
