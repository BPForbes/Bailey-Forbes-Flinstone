#include "cmd_decl.h"
#include "cmd_authutil.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "contract_p2_elevation.h"
#include "session_sync.h"
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

/* TODO(P2/Codex): sudo -i could reset login environment like su -; today only
 * fl_session_set_user("root") + sync (no separate login shell spawn). */
int cmd_sudo_interactive_login(void) {
    fl_result_t rc;

    if (cmd_sudo_require_auth() != 0)
        return 1;
    rc = fl_session_set_user("root");
    if (rc != FL_RESULT_OK) {
        fprintf(stderr, "sudo: cannot switch to root (%d)\n", (int)rc);
        return 1;
    }
    fl_session_sync_services();
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
