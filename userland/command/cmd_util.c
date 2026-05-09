#include "cmd_util.h"
#include "fs_jail.h"
#include <stdio.h>

/**
 * Determine whether a resolved filesystem path is blocked by the project's filesystem jail and report it.
 *
 * @param op Operation name included in the report (for example, "open").
 * @param input Original user-supplied path included in the report.
 * @param resolved Resolved/absolute path to validate against the filesystem jail.
 * @returns `1` if the path is considered blocked and a message was printed to stdout, `0` otherwise.
 */
int cmd_jail_blocked_path(const char *op, const char *input, const char *resolved) {
    if (!fs_jail_is_active())
        return 0;
    if (fs_jail_check_path(resolved) == 0)
        return 0;
    printf("VM: %s blocked (outside project sandbox): %s\n", op, input);
    return 1;
}
