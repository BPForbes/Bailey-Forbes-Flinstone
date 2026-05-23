#include "cmd_decl.h"
#include "fl/session.h"
#include <stdio.h>

int cmd_whoami_run(int argc, char **argv) {
    const char *user;
    int elev_acct;
    int has_elev;
    int sudo_scope;

    (void)argc;
    (void)argv;
    fl_session_init();
    user = fl_session_current_user();
    elev_acct = fl_session_is_elevated_account();
    has_elev = fl_session_has_elevation();
    sudo_scope = fl_session_in_sudo_scope();

    printf("%s", user);
    if (elev_acct)
        fputs(" (elevated account)", stdout);
    else if (has_elev)
        fputs(" (elevation active)", stdout);
    if (sudo_scope)
        fputs(" [sudo scope]", stdout);
    fputc('\n', stdout);
    return 0;
}
