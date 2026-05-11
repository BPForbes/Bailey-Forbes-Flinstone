#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

int cmd_help_maybe(const char *trimmed) {
    if (strcmp(trimmed, "help") != 0)
        return 0;
    printf("%s\n", HELP_MSG);
    return 1;
}
