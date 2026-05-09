#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

/**
 * Handle the "cc" command by clearing the saved history.
 *
 * If `trimmed` is exactly "cc", the function removes the HISTORY_FILE,
 * prints a confirmation message, sets the global `g_history_cleared` flag,
 * and indicates the command was handled.
 *
 * @param trimmed Input command string (expected to be trimmed of surrounding whitespace).
 * @return 1 if the command was handled (input exactly equals "cc"), 0 otherwise.
 */
int cmd_cc_maybe(const char *trimmed) {
    if (strcmp(trimmed, "cc") != 0)
        return 0;
    remove(HISTORY_FILE);
    printf("History cleared.\n");
    g_history_cleared = 1;
    return 1;
}
