#include "cmd_decl.h"
#include "fl/session.h"
#include "session_sync.h"
#include <stdio.h>

int cmd_login_run(int argc, char **argv) {
    fl_result_t rc;

    if (argc < 3) {
        fprintf(stderr, "login: usage: login <username> <password>\n");
        return 1;
    }
    rc = fl_session_login(argv[1], argv[2]);
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "login: failed for %s (%d)\n", argv[1], (int)rc);
        return 1;
    }
    fl_session_sync_services();
    printf("login: now %s%s\n", fl_session_current_user(),
           fl_session_is_elevated_account() ? " (elevated)" : "");
    return 0;
}
