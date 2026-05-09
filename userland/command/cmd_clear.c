#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

/**
 * Emit the terminal reset/clear escape sequence when the provided command is "clear".
 *
 * @param trimmed Null-terminated command string to check; compared against "clear".
 * @returns 1 if `trimmed` equals "clear" and the ANSI clear/reset sequence was written to stdout, 0 otherwise.
 */
int cmd_clear_maybe(const char *trimmed) {
    if (strcmp(trimmed, "clear") != 0)
        return 0;
    printf("\033c");
    return 1;
}
