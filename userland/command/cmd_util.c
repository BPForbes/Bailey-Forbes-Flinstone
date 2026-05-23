#include "cmd_util.h"
#include "fs_jail.h"
#include <stdio.h>

int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved) {
    if (!fs_jail_is_active())
        return 0;
    if (fs_jail_check_access(resolved) == 0)
        return 0;
    printf("VM: %s blocked (jail access policy): %s\n", op, input);
    return 1;
}
