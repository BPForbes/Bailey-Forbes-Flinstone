#include "cmd_decl.h"
#include "interpreter.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

int cmd_rerun_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: rerun <N>\n");
        return 1;
    }
    int idx = atoi(args[1]);
    if (idx <= 0) {
        printf("Index must be > 0.\n");
        return 1;
    }
    char *cmd = read_history_line(idx);
    if (!cmd) {
        printf("No such history line.\n");
        return 1;
    }
    printf("Re-executing command #%d: %s\n", idx, cmd);
    int rc = execute_command_str(cmd);
    free(cmd);
    return rc;
}
