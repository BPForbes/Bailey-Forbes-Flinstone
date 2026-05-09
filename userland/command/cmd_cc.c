#include "common.h"
#include "cmd_decl.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

int cmd_cc_maybe(const char *trimmed) {
    if (strcmp(trimmed, "cc") != 0)
        return 0;
    errno = 0;
    if (remove(HISTORY_FILE) == 0 || errno == ENOENT) {
        printf("History cleared.\n");
        g_history_cleared = 1;
    } else {
        perror("cc");
    }
    return 1;
}
