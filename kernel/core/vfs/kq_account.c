/*
 * Account unlock helpers (FL-15): open a KQST through the KeyQuorum driver using pairing
 * facts from the account store. Box-key ownership after open remains an owner decision;
 * callers that need the signing secret online must publish it themselves (key service hook).
 */
#include "fl/kq.h"

#include "fl/driver/keyquorum.h"
#include "mem_domain.h"
#include "session.h"
#include "user_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fl_kq_requirement_t fl_kq_unlock_requirement(const char *current_user, const char *target_user,
                                             int sudo) {
    fl_kq_requirement_t out = {.factor = FL_KQ_ACCOUNT_INVALID, .bridge_index = -1,
                               .needs_parent_grant = 0};

    (void)current_user;
    (void)target_user;
    (void)sudo;
    /* kq_access_requirement needs KeyQuorum ABI 4 (keyquorum_access.h). Until the pin
     * retargets after #180, callers must treat this as unavailable. */
    return out;
}

/**
 * Open the account's KQST. On OK, signing_secret_out holds the signing secret (caller wipes);
 * the encryption secret is wiped here (box-key ownership is not decided on this train).
 */
int fl_kq_open_account_secrets(const char *user, const char *password, const char *kqst_path_or_null,
                               uint8_t signing_secret_out[32]) {
    const fl_kq_driver_t *drv;
    fl_user_db_t *db;
    const fl_user_record_t *u;
    fl_user_record_t snapshot;
    char path[512];
    unsigned char device_id[16], signing_pub[32], encryption_pub[32];
    unsigned char encryption_secret[32];
    uint8_t *token = NULL;
    size_t tlen = 0;
    FILE *fp;
    long flen;
    int st;

    if (!user || !user[0] || !password || !signing_secret_out)
        return FL_KQ_ACCOUNT_INVALID;
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
                        encryption_pub, signing_secret_out, encryption_secret);
    mem_domain_secure_zero(token, tlen);
    free(token);
    mem_domain_secure_zero(encryption_secret, sizeof encryption_secret);
    if (st != FL_KQ_ACCOUNT_OK)
        mem_domain_secure_zero(signing_secret_out, 32);
    return st;
}

int fl_kq_open_account(const char *user, const char *password, const char *kqst_path_or_null) {
    uint8_t signing_secret[32];
    int st = fl_kq_open_account_secrets(user, password, kqst_path_or_null, signing_secret);

    mem_domain_secure_zero(signing_secret, sizeof signing_secret);
    return st;
}
