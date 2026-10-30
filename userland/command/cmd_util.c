#include "cmd_util.h"
#include "common.h"
#include "fs_jail.h"
#include "server_shared_fs.h"
#include "util.h"
#include <stdio.h>

int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved) {
    if (fl_server_shared_path_is_expired_quarantine(resolved ? resolved : input)) {
        printf("Access denied (expired server share quarantine): %s\n",
               input ? input : resolved);
        return 1;
    }
    if (!fs_jail_is_active())
        return 0;
    /* The access check below judges `resolved`. If the typed path cannot be resolved completely
     * (too many segments, too long) `resolved` may be a cut-down path that names another file, so
     * the jail refuses it rather than judge the wrong one. */
    if (input) {
        char whole[CWD_MAX];
        if (resolve_path_checked(input, whole, sizeof(whole)) != 0) {
            printf("VM: %s blocked (path too long to check): %s\n", op, input);
            return 1;
        }
    }
    if (fs_jail_check_access(resolved) == 0)
        return 0;
    printf("VM: %s blocked (jail access policy): %s\n", op, input);
    return 1;
}
