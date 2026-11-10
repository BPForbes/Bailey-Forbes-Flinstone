/*
 * Account unlock helpers (FL-15, FL-12): open a KQST through the KeyQuorum driver using pairing
 * facts from the account store, ask KeyQuorum which factor a switch demands (the live bridges are
 * the store's), and prove bridge membership against the store's current bridge facts. Box-key
 * ownership after open remains an owner decision; callers that need the signing secret online must
 * publish it themselves (key service hook).
 */
#include "fl/kq.h"

#include "fl/driver/keyquorum.h"
#include "fl/random.h"
#include "mem_domain.h"
#include "session.h"
#include "user_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user,
                                             int sudo) {
    fl_kq_requirement_t out = {.factor = FL_KQ_ACCESS_REFUSED, .bridge_index = -1,
                               .needs_parent_grant = 0, .bridge_uid = ""};
    const fl_kq_driver_t *drv = fl_kq_driver_get_or_register();
    fl_user_record_t cur, tgt;
    fl_user_bridge_t *live;
    kq_bridge_t bridges[FL_USER_BRIDGES_MAX];
    const char *labels[FL_USER_BRIDGES_MAX][FL_USER_BRIDGE_MEMBERS_MAX];
    size_t n = 0, i, j;
    int factor, idx = -1, grant = 0;

    if (!drv || !drv->access_requirement || !current_user || !target_user)
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
    for (i = 0; i < n; i++) {
        for (j = 0; j < live[i].nmembers; j++)
            labels[i][j] = live[i].members[j].label;
        bridges[i].uid = live[i].uid;
        bridges[i].members = labels[i];
        bridges[i].nmembers = live[i].nmembers;
    }
    /* parent_approval 0: no unlock_approval policy is stored on this side. */
    factor = drv->access_requirement(cur.key_id, tgt.key_id, sudo ? 1 : 0, n ? bridges : NULL, n, 0,
                                     &idx, &grant);
    switch (factor) {
    case FL_KQ_ACCESS_NONE:
    case FL_KQ_ACCESS_PASSWORD:
    case FL_KQ_ACCESS_KQST_PASSWORD:
        out.factor = factor;
        out.needs_parent_grant = grant ? 1 : 0;
        break;
    case FL_KQ_ACCESS_PASSWORD_BRIDGE_KQST:
        if (idx >= 0 && (size_t)idx < n) {
            out.factor = factor;
            out.bridge_index = idx;
            out.needs_parent_grant = grant ? 1 : 0;
            memcpy(out.bridge_uid, live[idx].uid, strlen(live[idx].uid) + 1);
        }
        break;
    default:
        break; /* REFUSED, INVALID or a factor this build does not know: refused */
    }
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

int fl_kq_prove_bridge_member(const char *uid, const char *member_user, const char *member_password) {
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

    if (!drv || !drv->bridge_prove || !drv->bridge_verify || !uid || !uid[0] || !member_user ||
        !member_password)
        return FL_KQ_BRIDGE_INVALID;
    if (fl_session_snapshot_account(member_user, &member) != FL_RESULT_OK)
        return FL_KQ_BRIDGE_REFUSED;
    facts = (fl_user_bridge_t *)calloc(1, sizeof *facts);
    if (!facts)
        return FL_KQ_BRIDGE_INVALID;
    /* The facts are read here, from the store, at the moment of the proof. */
    if (fl_user_db_bridge_get(fl_session_user_db(), uid, facts) != FL_RESULT_OK) {
        st = FL_KQ_BRIDGE_STALE;
        goto out;
    }
    for (i = 0; i < facts->nmembers; i++) {
        labels[i] = facts->members[i].label;
        memcpy(pubs + i * 32, facts->members[i].signing_public, 32);
        if (strcmp(facts->members[i].label, member.key_id) == 0)
            in_roster = 1;
    }
    if (facts->destroyed) {
        st = FL_KQ_BRIDGE_STALE;
        goto out;
    }
    if (!in_roster || fl_user_db_bridge_sealed_key(fl_session_user_db(), uid, member.key_id, sealed,
                                                   sizeof sealed, &slen, wrap_salt) != FL_RESULT_OK) {
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
    return st;
}

/**
 * Open the account's KQST. On OK, both secrets are written (caller wipes); otherwise both are zeroed.
 */
int fl_kq_open_account_keys(const char *user, const char *password, const char *kqst_path_or_null,
                            uint8_t signing_secret_out[32], uint8_t encryption_secret_out[32]) {
    const fl_kq_driver_t *drv;
    fl_user_db_t *db;
    const fl_user_record_t *u;
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
    db = fl_session_user_db_mut();
    if (!db)
        return FL_KQ_ACCOUNT_INVALID;
    u = fl_user_db_find(db, user);
    if (!u)
        return FL_KQ_ACCOUNT_INVALID;
    snapshot = *u;
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
