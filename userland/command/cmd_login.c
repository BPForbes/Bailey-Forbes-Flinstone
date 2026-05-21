#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "fl/session.h"
#include "session_sync.h"
#include <stdio.h>

int cmd_login_run(int argc, char **argv) {
    fl_result_t rc;
    char pw[128];

    if (argc < 2) {
        fprintf(stderr, "login: usage: login <username>\n");
        return 1;
    }
    if (argc >= 3) {
        fprintf(stderr,
                "login: password is read on the next line (not as a command argument)\n");
    }
    if (cmd_read_password("Password: ", pw, sizeof(pw)) != 0) {
        fprintf(stderr, "login: password read failed\n");
        return 1;
    }
    rc = fl_session_login(argv[1], pw);
    cmd_wipe_password(pw, sizeof(pw));
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "login: failed for %s (%d)\n", argv[1], (int)rc);
        return 1;
    }
    fl_session_sync_services();
    printf("login: now %s%s\n", fl_session_current_user(),
           fl_session_is_elevated_account() ? " (elevated)" : "");
    return 0;
}
