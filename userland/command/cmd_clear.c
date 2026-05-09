#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>

int cmd_clear_maybe(const char *trimmed) {
    if (strcmp(trimmed, "clear") != 0)
        return 0;
    printf("\033c");
    return 1;
}
