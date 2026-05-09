#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

/**
 * Handle the "help" command by printing the help message when matched.
 *
 * If `trimmed` is exactly "help", prints `HELP_MSG` followed by a newline to stdout.
 *
 * @param trimmed Command token to check.
 * @returns `1` if `trimmed` equals "help" (help message printed), `0` otherwise.
 */
int cmd_help_maybe(const char *trimmed) {
    if (strcmp(trimmed, "help") != 0)
        return 0;
    printf("%s\n", HELP_MSG);
    return 1;
}
