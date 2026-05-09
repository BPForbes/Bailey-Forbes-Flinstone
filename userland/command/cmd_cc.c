#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

int cmd_cc_maybe(const char *trimmed) {
    if (strcmp(trimmed, "cc") != 0)
        return 0;
    remove(HISTORY_FILE);
    printf("History cleared.\n");
    g_history_cleared = 1;
    return 1;
}
