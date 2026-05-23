#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include <stdio.h>
#include <string.h>

int cmd_help_maybe(const char *trimmed) {
    if (strcmp(trimmed, "help") != 0)
        return 0;
    fl_print_help_message();
    return 1;
}

int cmd_help_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 1;
}
