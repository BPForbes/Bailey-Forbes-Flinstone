#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_authutil.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "contract_p2_elevation.h"
#include "session_sync.h"
#include "session_login_env.h"
#include "user_db.h"
#include "heritage.h"
#include <stdio.h>
#include <string.h>

static int sudo_grant_token(const char *reason) {
    int had_elev;

    fl_session_init();
    if (fl_session_is_elevated_account()) {
        printf("sudo: already elevated as %s\n", fl_session_current_user());
        return 0;
    }
    had_elev = fl_session_has_elevation();
    if (cmd_sudo_require_auth_reason(reason) != 0)
        return 1;
    if (had_elev) {
        printf("sudo: elevation already active\n");
        return 0;
    }
    printf("sudo: elevation granted (ttl<=%us)\n",
           (unsigned)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS);
    printf("sudo: use `sudo <command>` to escape VM jail (token alone does not)\n");
    return 0;
}

/*
 * sudo -i becomes Root, the master of the whole heritage tree, so it asks for
 * Root's own password, as su root does, never the caller's. An elevated
 * account is already Root or sits at its level and needs no second prompt.
 */
int cmd_sudo_interactive_login(void) {
    const fl_user_db_t *db;
    const fl_user_record_t *root;
    char actor[FL_HERITAGE_KEY_ID_MAX];
    char pw[128];
    fl_result_t rc;

    fl_session_init();
    db = fl_session_user_db();
    root = fl_user_db_find_by_key(db, FL_HERITAGE_ROOT_KEY);
    if (!root) {
        fprintf(stderr, "sudo: no Root account\n");
        return 1;
    }
    if (!fl_session_is_elevated_account()) {
        if (cmd_read_password("[sudo -i] Root password: ", pw, sizeof(pw)) != 0) {
            cmd_wipe_password(pw, sizeof(pw));
            fprintf(stderr, "sudo: password read failed\n");
            return 1;
        }
        if (!fl_user_db_verify_password(db, root->name, pw)) {
            cmd_wipe_password(pw, sizeof(pw));
            fprintf(stderr, "sudo: incorrect Root password\n");
            return 1;
        }
        cmd_wipe_password(pw, sizeof(pw));
    }
    strncpy(actor, fl_session_current_key_id(), sizeof(actor) - 1);
    actor[sizeof(actor) - 1] = '\0';
    rc = fl_session_set_user(root->name);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo: cannot switch to root (%d)\n", (int)rc);
        return 1;
    }
    fl_audit_identity_event(actor, "sudo-i", FL_HERITAGE_ROOT_KEY, NULL);
    fl_session_sync_services();
    fl_session_apply_login_shell_env(root->name);
    printf("sudo: switched to root (login shell)\n");
    return 0;
}

static int sudo_revoke_elevation(void) {
    const char *user;

    fl_session_init();
    user = fl_session_current_user();
    if (fl_session_has_elevation() && !fl_session_is_elevated_account())
        fl_audit_elevation_event(user, "sudo -k", 0);
    fl_session_drop_elevation();
    printf("sudo: elevation revoked\n");
    return 0;
}

int cmd_sudo_run(int argc, char **argv) {
    const char *reason = "lab elevation";

    if (argc >= 2 && argv[1] && strcmp(argv[1], "-k") == 0) {
        if (argc > 2) {
            fprintf(stderr, "sudo: -k does not take additional arguments\n");
            return 1;
        }
        return sudo_revoke_elevation();
    }

    if (argc >= 2 && argv[1] && strcmp(argv[1], "-i") == 0)
        return cmd_sudo_interactive_login();

    if (argv[1])
        reason = argv[1];
    return sudo_grant_token(reason);
}

int cmd_sudo_batch_tokens_count(int argc, char **argv, int i) {
    int end;

    if (i >= argc || !argv[i] || strcmp(argv[i], "sudo"))
        return 1;
    if (i + 1 < argc && argv[i + 1] &&
        (!strcmp(argv[i + 1], "-i") || !strcmp(argv[i + 1], "-k")))
        return 2;
    for (end = argc; end > i + 1; end--) {
        if (fl_batch_argv_layout_valid(argc, argv, i, end))
            return end - i;
    }
    return 1;
}
