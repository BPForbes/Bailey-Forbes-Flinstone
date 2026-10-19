#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "cmd_batch.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "bridge.h"
#include "fs_jail.h"
#include "heritage.h"
#include "timekeeping.h"
#include "user_db.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int ensure_privilege(void) {
    if (fl_session_has_elevation())
        return 0;
    fprintf(stderr, "permission denied (login as root, run `sudo`, or prefix with `sudo`)\n");
    return 1;
}

/** Drop in-memory account mutations when persistence fails. */
static int account_reload_db_from_disk(fl_user_db_t *db) {
    if (!db || !db->path[0])
        return 0;
    if (fl_user_db_load(db, db->path) != FL_RESULT_OK) {
        fprintf(stderr, "account: could not reload %s from disk\n", db->path);
        return 1;
    }
    return 0;
}

int cmd_useradd_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *name;
    char pw[128];
    const char *pass;
    uint32_t uid = 2000;
    char creator[FL_HERITAGE_KEY_ID_MAX];
    int64_t now = 0;
    const fl_user_record_t *u;

    if (ensure_privilege())
        return 1;
    if (argc < 2) {
        fprintf(stderr, "useradd: usage: useradd <username>\n");
        return 1;
    }
    if (argc >= 3) {
        fprintf(stderr,
                "useradd: password is read on the next line (not as a command argument)\n");
    }
    name = argv[1];
    /* The new account descends from whoever is logged in (sudo does not change that). */
    strncpy(creator, fl_session_current_key_id(), sizeof(creator) - 1);
    creator[sizeof(creator) - 1] = '\0';
    if (!fl_heritage_key_valid(creator)) {
        fprintf(stderr, "useradd: the logged-in account has no heritage key id\n");
        return 1;
    }
    if (!fl_heritage_name_available_shape(name)) {
        fprintf(stderr, "useradd: account names are 1-%u letters, digits, '_' or '-', and not Root\n",
                (unsigned)FL_HERITAGE_SEGMENT_MAX_CHARS);
        return 1;
    }
    if (fl_heritage_depth(creator) >= FL_HERITAGE_MAX_DEPTH) {
        fprintf(stderr, "useradd: %s is at the deepest heritage level (%u)\n", creator,
                (unsigned)FL_HERITAGE_MAX_DEPTH);
        return 1;
    }
    if (cmd_read_password("New password: ", pw, sizeof(pw)) != 0) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "useradd: password read failed\n");
        return 1;
    }
    pass = pw;
    if (fl_time_wall_sec(&now) != FL_RESULT_OK)
        now = 0;
    db = fl_session_user_db_mut();
    uid = fl_user_db_next_uid(db);
    rc = fl_user_db_add_user_under(db, creator, name, pass, uid, 0, now);
    cmd_wipe_password(pw, sizeof(pw));
    if (rc == FL_RESULT_BUSY) {
        fprintf(stderr, "useradd: %s already exists\n", name);
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "useradd: failed (%d)\n", (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK) {
        (void)account_reload_db_from_disk(db);
        fprintf(stderr, "useradd: could not save %s\n", FL_SESSION_USERS_PATH);
        return 1;
    }
    u = fl_user_db_find(db, name);
    fl_audit_identity_event(creator, "useradd", u ? u->key_id : name, NULL);
    printf("useradd: added %s (uid=%u, key %s)\n", name, (unsigned)uid, u ? u->key_id : "?");
    if (u && fs_jail_is_active()) {
        char home[4096];

        if (fs_jail_account_home(u->key_id, home, sizeof home) == 0)
            printf("useradd: home %s\n", home);
    }
    return 0;
}

int cmd_userdel_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const fl_user_record_t *target;
    char actor[FL_HERITAGE_KEY_ID_MAX];
    char target_key[FL_HERITAGE_KEY_ID_MAX];

    if (ensure_privilege())
        return 1;
    if (argc < 2) {
        fprintf(stderr, "userdel: usage: userdel <username>\n");
        return 1;
    }
    if (!strcasecmp(argv[1], "root")) {
        fprintf(stderr, "userdel: cannot remove Root\n");
        return 1;
    }
    db = fl_session_user_db_mut();
    target = fl_user_db_find(db, argv[1]);
    if (!target) {
        fprintf(stderr, "userdel: %s does not exist\n", argv[1]);
        return 1;
    }
    strncpy(actor, fl_session_current_key_id(), sizeof(actor) - 1);
    actor[sizeof(actor) - 1] = '\0';
    strncpy(target_key, target->key_id, sizeof(target_key) - 1);
    target_key[sizeof(target_key) - 1] = '\0';
    /* Only an ancestor removes an account: never yourself, a sibling or another branch. */
    if (fl_heritage_relation(actor, target_key) != FL_HERITAGE_REL_DESCENDANT) {
        fprintf(stderr, "userdel: %s (%s) is not below %s\n", argv[1], target_key,
                actor[0] ? actor : "this account");
        return 1;
    }
    rc = fl_user_db_remove_user(db, argv[1]);
    if (rc == FL_RESULT_BUSY) {
        fprintf(stderr, "userdel: %s still has accounts below it; remove those first\n", argv[1]);
        return 1;
    }
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "userdel: %s (%d)\n", argv[1], (int)rc);
        return 1;
    }
    /*
     * Bridges go before the account does. A bridge row left behind for a removed
     * key would give the next account created under that name the old cross-branch
     * access. If the bridges cannot be removed, the account stays; if the save then
     * fails, the account stays too, without its bridges (fail closed).
     */
    if (fl_bridge_remove_for(db->path, target_key) != FL_RESULT_OK) {
        (void)account_reload_db_from_disk(db);
        fprintf(stderr, "userdel: could not remove the bridges of %s; %s is unchanged\n", target_key,
                argv[1]);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK) {
        (void)account_reload_db_from_disk(db);
        fprintf(stderr, "userdel: could not save %s; %s is kept, and its bridges must be requested again\n",
                FL_SESSION_USERS_PATH, argv[1]);
        return 1;
    }
    fl_audit_identity_event(actor, "userdel", target_key, NULL);
    printf("userdel: removed %s (%s)\n", argv[1], target_key);
    return 0;
}

int cmd_passwd_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *user;
    const fl_user_record_t *target;
    char actor[FL_HERITAGE_KEY_ID_MAX];
    char target_key[FL_HERITAGE_KEY_ID_MAX];
    fl_heritage_relation_t rel;
    char pw[128];
    const char *pass;

    if (ensure_privilege())
        return 1;
    user = fl_session_current_user();
    if (argc >= 2)
        user = argv[1];
    if (argc >= 3) {
        fprintf(stderr,
                "passwd: password is read on the next line (not as a command argument)\n");
    }
    target = fl_user_db_find(fl_session_user_db(), user);
    if (!target) {
        fprintf(stderr, "passwd: %s does not exist\n", user);
        return 1;
    }
    strncpy(actor, fl_session_current_key_id(), sizeof(actor) - 1);
    actor[sizeof(actor) - 1] = '\0';
    strncpy(target_key, target->key_id, sizeof(target_key) - 1);
    target_key[sizeof(target_key) - 1] = '\0';
    rel = fl_heritage_relation(actor, target_key);
    if (rel != FL_HERITAGE_REL_SELF && rel != FL_HERITAGE_REL_DESCENDANT) {
        fprintf(stderr, "passwd: %s (%s) is not this account or below it\n", user, target_key);
        return 1;
    }
    if (cmd_read_password("New password: ", pw, sizeof(pw)) != 0) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "passwd: password read failed\n");
        return 1;
    }
    pass = pw;
    db = fl_session_user_db_mut();
    rc = fl_user_db_set_password(db, user, pass);
    cmd_wipe_password(pw, sizeof(pw));
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "passwd: failed for %s (%d)\n", user, (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK) {
        (void)account_reload_db_from_disk(db);
        fprintf(stderr, "passwd: could not save %s\n", FL_SESSION_USERS_PATH);
        return 1;
    }
    fl_audit_identity_event(actor, "passwd", target_key, NULL);
    printf("passwd: password updated for %s\n", user);
    return 0;
}

int cmd_useradd_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-' &&
        !cmd_batch_token_is_shell_command(argv[i + 1]))
        return 2;
    return 1;
}

int cmd_userdel_batch_tokens_count(int argc, char **argv, int i) {
    if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-' &&
        !cmd_batch_token_is_shell_command(argv[i + 1]))
        return 2;
    return 1;
}

int cmd_passwd_batch_tokens_count(int argc, char **argv, int i) {
    return cmd_batch_opt_path_arity(argc, argv, i);
}
