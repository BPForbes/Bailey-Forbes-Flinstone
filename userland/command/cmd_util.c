#include "cmd_util.h"
#include "fs_jail.h"
#include "server_shared_fs.h"
#include <stdio.h>

int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved) {
    if (fl_server_shared_path_is_expired_quarantine(resolved ? resolved : input)) {
        printf("Access denied (expired server share quarantine): %s\n",
               input ? input : resolved);
        return 1;
    }
    if (!fs_jail_is_active())
        return 0;
    if (fs_jail_check_access(resolved) == 0)
        return 0;
    printf("VM: %s blocked (jail access policy): %s\n", op, input);
    return 1;
}
