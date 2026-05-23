#include "common.h"
#include "cmd_decl.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

int cmd_cc_maybe(const char *trimmed) {
    if (strcmp(trimmed, "cc") != 0)
        return 0;
    if (delete_history_storage() != 0)
        perror("cc");
    else {
        printf("History cleared.\n");
        g_history_cleared = 1;
    }
    return 1;
}
