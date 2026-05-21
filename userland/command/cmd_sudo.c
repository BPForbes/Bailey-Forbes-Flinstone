#include "cmd_decl.h"
#include "fl/session.h"
#include "fl/elevation.h"
#include "contract_p2_elevation.h"
#include <stdio.h>

int cmd_sudo_run(int argc, char **argv) {
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
    printf("sudo: use `sudo <command>` to escape VM jail (token alone does not)\n");
    return 0;
}
