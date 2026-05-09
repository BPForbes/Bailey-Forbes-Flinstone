#include "cmd_util.h"
#include "fs_jail.h"
#include <stdio.h>

int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved) {
    if (!fs_jail_is_active())
        return 0;
    if (fs_jail_check_path(resolved) == 0)
        return 0;
    printf("VM: %s blocked (outside project sandbox): %s\n", op, input);
    return 1;
}
