#include "cmd_decl.h"
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
    const char *pass;
    uint32_t uid = 2000;

    if (ensure_privilege())
        return 1;
    if (argc < 2) {
        fprintf(stderr, "useradd: usage: useradd <username> [password]\n");
        return 1;
    }
    name = argv[1];
    pass = (argc >= 3 && argv[2]) ? argv[2] : name;
    db = fl_session_user_db_mut();
    if (db->count > 0)
        uid = db->users[db->count - 1].uid + 1;
    rc = fl_user_db_add_user(db, name, pass, uid, 0);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "useradd: failed (%d)\n", (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "useradd: warning: could not save %s\n", FL_SESSION_USERS_PATH);
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
    db = fl_session_user_db_mut();
    rc = fl_user_db_remove_user(db, argv[1]);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "userdel: %s (%d)\n", argv[1], (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "userdel: warning: could not save %s\n", FL_SESSION_USERS_PATH);
    printf("userdel: removed %s\n", argv[1]);
    return 0;
}

int cmd_passwd_run(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *user;
    const char *pass;

    if (ensure_privilege())
        return 1;
    user = fl_session_current_user();
    pass = NULL;
    if (argc >= 2)
        user = argv[1];
    if (argc >= 3)
        pass = argv[2];
    if (!pass) {
        fprintf(stderr, "passwd: usage: passwd [user] <newpassword>\n");
        return 1;
    }
    db = fl_session_user_db_mut();
    rc = fl_user_db_set_password(db, user, pass);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "passwd: failed for %s (%d)\n", user, (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "passwd: warning: could not save %s\n", FL_SESSION_USERS_PATH);
    printf("passwd: password updated for %s\n", user);
    return 0;
}
