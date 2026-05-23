#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "fl/session.h"
#include <stdio.h>
#include <string.h>

static int ensure_privilege(void) {
    if (fl_session_has_elevation())
        return 0;
    fprintf(stderr, "permission denied (login as root, run `sudo`, or prefix with `sudo`)\n");
    return 1;
}

int cmd_useradd_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *name;
    char pw[128];
    const char *pass;
    uint32_t uid = 2000;

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
    if (cmd_read_password("New password: ", pw, sizeof(pw)) != 0) {
        cmd_wipe_password(pw, sizeof(pw));
        fprintf(stderr, "useradd: password read failed\n");
        return 1;
    }
    pass = pw;
    db = fl_session_user_db_mut();
    uid = fl_user_db_next_uid(db);
    rc = fl_user_db_add_user(db, name, pass, uid, 0);
    cmd_wipe_password(pw, sizeof(pw));
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "useradd: failed (%d)\n", (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK) {
        fprintf(stderr, "useradd: could not save %s\n", FL_SESSION_USERS_PATH);
        return 1;
    }
    printf("useradd: added %s (uid=%u)\n", name, (unsigned)uid);
    return 0;
}

int cmd_userdel_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;

    if (ensure_privilege())
        return 1;
    if (argc < 2) {
        fprintf(stderr, "userdel: usage: userdel <username>\n");
        return 1;
    }
    if (!strcmp(argv[1], "root")) {
        fprintf(stderr, "userdel: cannot remove root\n");
        return 1;
    }
    db = fl_session_user_db_mut();
    rc = fl_user_db_remove_user(db, argv[1]);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "userdel: %s (%d)\n", argv[1], (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK) {
        fprintf(stderr, "userdel: could not save %s\n", FL_SESSION_USERS_PATH);
        return 1;
    }
    printf("userdel: removed %s\n", argv[1]);
    return 0;
}

int cmd_passwd_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *user;
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
        fprintf(stderr, "passwd: could not save %s\n", FL_SESSION_USERS_PATH);
        return 1;
    }
    printf("passwd: password updated for %s\n", user);
    return 0;
}
