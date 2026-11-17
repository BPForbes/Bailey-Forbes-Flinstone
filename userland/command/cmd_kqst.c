/* kqst: pair an account with its KeyQuorum slot token; the password and the opening token are the proof. */
#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "cmd_batch.h"
#include "fl/hex.h"
#include "fl/session.h"
#include "heritage.h"
#include "timekeeping.h"
#include "user_db.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kqst_status(const char *name) {
    fl_user_record_t rec;
    fl_user_kqst_row_t *row;
    char dev[33];
    int rc = 1;

    if (fl_session_snapshot_account(name, &rec) != FL_RESULT_OK) {
        fprintf(stderr, "kqst: %s does not exist\n", name);
        return 1;
    }
    row = (fl_user_kqst_row_t *)calloc(1, sizeof *row);
    if (!row || fl_user_db_kqst_row_get(fl_session_user_db(), rec.key_id, row) != FL_RESULT_OK) {
        fprintf(stderr, "kqst: cannot read the account store\n");
        goto out;
    }
    if (rec.auth_mode == FL_USER_AUTH_PASSWORD_KQST && row->present) {
        fl_hex_encode(row->device_id, sizeof row->device_id, dev);
        printf("kqst: %s (%s) logs in with its password and token (device %s, generation %lld)\n", rec.name,
               rec.key_id, dev, (long long)row->generation);
    } else if (rec.auth_mode == FL_USER_AUTH_PASSWORD_KQST) {
        printf("kqst: %s (%s) is set to log in with a token, but none is paired\n", rec.name, rec.key_id);
    } else {
        printf("kqst: %s (%s) logs in with its password only (no token)\n", rec.name, rec.key_id);
    }
    rc = 0;
out:
    free(row);
    return rc;
}

static int kqst_paired(const char *name, const char *key, int64_t generation) {
    printf("kqst: %s (%s) now logs in with its password and token (generation %lld)\n", name, key,
           (long long)generation);
    if (!strcmp(key, FL_HERITAGE_ROOT_KEY))
        printf("kqst: sudo now asks for Root's password and the caller's, and both tokens co-sign it\n");
    return 0;
}

static int kqst_enroll(const char *token, const char *dev_hex, const char *sig_hex, const char *enc_hex) {
    unsigned char dev[16], sig[32], enc[32];
    char path[PATH_MAX], pw[128], key[FL_HERITAGE_KEY_ID_MAX];
    const char *name = fl_session_current_user();
    int64_t now = 0, generation = 0;
    fl_result_t rc;

    if (fl_hex_decode(dev_hex, dev, sizeof dev) != 0) {
        fprintf(stderr, "kqst: device id must be 32 hex digits\n");
        return 1;
    }
    if (fl_hex_decode(sig_hex, sig, sizeof sig) != 0 || fl_hex_decode(enc_hex, enc, sizeof enc) != 0) {
        fprintf(stderr, "kqst: each public key must be 64 hex digits\n");
        return 1;
    }
    if (!realpath(token, path)) {
        fprintf(stderr, "kqst: cannot read %s\n", token);
        return 1;
    }
    if (strlen(path) >= FL_USER_KQST_PATH_MAX) {
        fprintf(stderr, "kqst: token path is too long\n");
        return 1;
    }
    snprintf(key, sizeof key, "%s", fl_session_current_key_id());
    if (cmd_read_password("Password (also opens the token): ", pw, sizeof pw) != 0) {
        cmd_wipe_password(pw, sizeof pw);
        fprintf(stderr, "kqst: password read failed\n");
        return 1;
    }
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    rc = fl_session_enroll_kqst(pw, path, dev, sig, enc, now, &generation);
    cmd_wipe_password(pw, sizeof pw);
    switch (rc) {
    case FL_RESULT_OK:
        return kqst_paired(name, key, generation);
    case FL_RESULT_BUSY:
        fprintf(stderr, "kqst: the login changed meanwhile; nothing was paired\n");
        return 1;
    case FL_RESULT_ERR:
        fprintf(stderr, "kqst: could not save the account store; nothing was paired\n");
        return 1;
    default:
        fprintf(stderr, "kqst: the token did not open with this account's password and those facts\n");
        return 1;
    }
}

/* kqst enroll with no arguments: the kernel makes and pairs the logged-in account's token. */
static int kqst_make(void) {
    char name[FL_USER_NAME_MAX], key[FL_HERITAGE_KEY_ID_MAX], pw[256];
    int64_t now = 0, generation = 0;
    fl_result_t rc;

    snprintf(name, sizeof name, "%s", fl_session_current_user());
    snprintf(key, sizeof key, "%s", fl_session_current_key_id());
    if (cmd_read_password("Password: ", pw, sizeof pw) != 0) {
        cmd_wipe_password(pw, sizeof pw);
        fprintf(stderr, "kqst: password read failed\n");
        return 1;
    }
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    rc = fl_session_make_kqst(name, pw, now, &generation);
    cmd_wipe_password(pw, sizeof pw);
    switch (rc) {
    case FL_RESULT_OK:
        return kqst_paired(name, key, generation);
    case FL_RESULT_NOSYS:
        fprintf(stderr, "kqst: this build has no KeyQuorum driver to make a token with\n");
        return 1;
    case FL_RESULT_ERR:
        fprintf(stderr, "kqst: could not make or save the token; nothing was paired\n");
        return 1;
    default:
        fprintf(stderr, "kqst: %s already has a token, or the password is wrong\n", name);
        return 1;
    }
}

int cmd_kqst_run(int argc, char **argv) {
    fl_session_init();
    if (!fl_session_users_loaded()) {
        fprintf(stderr, "kqst: account store is not available\n");
        return 1;
    }
    if (argc < 2 || !strcmp(argv[1], "status")) {
        const char *who = (argc >= 3) ? argv[2] : fl_session_current_user();

        return kqst_status(who);
    }
    if (!strcmp(argv[1], "enroll") && argc == 2)
        return kqst_make();
    if (!strcmp(argv[1], "enroll") && argc == 6)
        return kqst_enroll(argv[2], argv[3], argv[4], argv[5]);
    fprintf(stderr, "usage: kqst [status [account]]\n"
                    "       kqst enroll\n"
                    "       kqst enroll <token.kqst> <device-id> <signing-public> <encryption-public>\n");
    return 1;
}

int cmd_kqst_batch_tokens_count(int argc, char **argv, int i) {
    int n = 1, max = 1, k;

    if (i + 1 >= argc || !argv[i + 1] || cmd_batch_token_is_shell_command(argv[i + 1]))
        return 1;
    if (!strcmp(argv[i + 1], "status"))
        max = 3; /* kqst status [account] */
    else if (!strcmp(argv[i + 1], "enroll"))
        max = 6; /* kqst enroll <token> <device> <signing> <encryption> */
    else
        return 1;
    n = 2;
    for (k = i + 2; n < max && k < argc && argv[k] && !cmd_batch_token_is_shell_command(argv[k]); k++)
        n++;
    return n;
}
