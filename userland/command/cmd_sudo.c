#include "cmd_decl.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "contract_p2_elevation.h"
#include <stdio.h>
#include <string.h>

static int ensure_privilege(void) {
    if (fl_session_has_elevation())
        return 0;
    fprintf(stderr, "sudo: permission denied (run plain `sudo` first or login as an elevated account)\n");
    return 1;
}

static int sudo_grant(int argc, char **argv) {
    fl_elevation_token_t tok = FL_ELEVATION_TOKEN_NONE;
    const char *reason = "lab elevation";
    fl_result_t rc;
    (void)argc;
    if (argv[1])
        reason = argv[1];
    if (fl_session_is_elevated_account()) {
        printf("sudo: already elevated as %s\n", fl_session_current_user());
        return 0;
    }
    rc = fl_session_grant_elevation(reason, &tok);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo: elevation failed (%d)\n", (int)rc);
        return 1;
    }
    printf("sudo: elevation granted (token=%llu, ttl<=%us)\n",
           (unsigned long long)tok, (unsigned)FL_ELEVATION_LAB_TTL_SOFT_MAX_SECONDS);
    return 0;
}

static int sudo_useradd(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *name;
    const char *pass;
    uint32_t uid = 2000;

    if (ensure_privilege())
        return 1;
    if (argc < 3) {
        fprintf(stderr, "sudo useradd: usage: sudo useradd <username> [password]\n");
        return 1;
    }
    name = argv[2];
    pass = (argc >= 4 && argv[3]) ? argv[3] : name;
    db = fl_session_user_db_mut();
    if (db->count > 0)
        uid = db->users[db->count - 1].uid + 1;
    rc = fl_user_db_add_user(db, name, pass, uid, 0);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo useradd: failed (%d)\n", (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "sudo useradd: warning: could not save users.lab.json\n");
    printf("sudo useradd: added %s (uid=%u)\n", name, (unsigned)uid);
    return 0;
}

static int sudo_userdel(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;

    if (ensure_privilege())
        return 1;
    if (argc < 3) {
        fprintf(stderr, "sudo userdel: usage: sudo userdel <username>\n");
        return 1;
    }
    db = fl_session_user_db_mut();
    rc = fl_user_db_remove_user(db, argv[2]);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo userdel: %s (%d)\n", argv[2], (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "sudo userdel: warning: could not save users.lab.json\n");
    printf("sudo userdel: removed %s\n", argv[2]);
    return 0;
}

static int sudo_passwd(int argc, char **argv) {
    fl_user_db_t *db;
    fl_result_t rc;
    const char *user;
    const char *pass;

    if (ensure_privilege())
        return 1;
    user = fl_session_current_user();
    pass = NULL;
    if (argc >= 3)
        user = argv[2];
    if (argc >= 4)
        pass = argv[3];
    if (!pass) {
        fprintf(stderr, "sudo passwd: usage: sudo passwd [user] <newpassword>\n");
        return 1;
    }
    db = fl_session_user_db_mut();
    rc = fl_user_db_set_password(db, user, pass);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo passwd: failed for %s (%d)\n", user, (int)rc);
        return 1;
    }
    if (fl_session_save_users() != FL_RESULT_OK)
        fprintf(stderr, "sudo passwd: warning: could not save users.lab.json\n");
    printf("sudo passwd: password updated for %s\n", user);
    return 0;
}

int cmd_sudo_run(int argc, char **argv) {
    if (argc < 2)
        return sudo_grant(argc, argv);
    if (!strcmp(argv[1], "useradd"))
        return sudo_useradd(argc, argv);
    if (!strcmp(argv[1], "userdel"))
        return sudo_userdel(argc, argv);
    if (!strcmp(argv[1], "passwd"))
        return sudo_passwd(argc, argv);
    return sudo_grant(argc, argv);
}
