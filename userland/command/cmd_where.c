#include "cmd_decl.h"
#include "path_log.h"
#include <stdio.h>
#include <stdlib.h>

int cmd_where_run(int argc, char **argv) {
    char **args = argv;
    int n = 16;
    if (argc >= 2) {
        char *end = NULL;
        long parsed = strtol(args[1], &end, 10);
        if (end == args[1] || *end != '\0' || parsed <= 0 || parsed > (long)PATH_LOG_MAX) {
            printf("Usage: where [count]\n");
            printf("count must be an integer from 1 to %d.\n", PATH_LOG_MAX);
            return 1;
        }
        n = (int)parsed;
    }
    path_log_print(n);
    return 0;
}
