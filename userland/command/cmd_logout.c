#include "cmd_decl.h"
#include "fl/audit_log.h"
#include "fl/session.h"
#include "session_sync.h"
#include <stdio.h>

int cmd_logout_run(int argc, char **argv) {
    const char *before;

    (void)argc;
    (void)argv;
    fl_session_init();
    before = fl_session_current_user();
    /* TODO(P2/Codex): Emit elevation revoke audit after fl_session_logout() succeeds
     * so failed logout does not log a revoke (CodeRabbit). */
    if (fl_session_has_elevation() && !fl_session_is_elevated_account())
        fl_audit_elevation_event(before, "logout", 0);
    if (fl_session_logout() != FL_RESULT_OK) {
        fprintf(stderr, "logout: failed\n");
        return 1;
    }
    fl_session_sync_services();
    printf("logout: now %s\n", fl_session_current_user());
    return 0;
}
