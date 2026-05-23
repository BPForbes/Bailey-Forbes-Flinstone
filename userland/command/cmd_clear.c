#include "cmd_decl.h"
#include "cmd_batch.h"
#include <stdio.h>
#include <string.h>

int cmd_clear_maybe(const char *trimmed) {
    if (strcmp(trimmed, "clear") != 0)
        return 0;
    printf("\033c");
    return 1;
}

int cmd_clear_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
