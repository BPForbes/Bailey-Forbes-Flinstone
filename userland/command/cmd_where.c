#include "cmd_decl.h"
#include "path_log.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Handle the "where" command: determine a count from the first argument and invoke path_log_print.
 *
 * If argc >= 2, parses argv[1] with atoi to obtain the count n; otherwise uses 16. Calls path_log_print(n).
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector; argv[1], if present, is parsed as the integer count.
 * @returns 0 on success.
 */
int cmd_where_run(int argc, char **argv) {
    char **args = argv;
    int n = (argc >= 2) ? atoi(args[1]) : 16;
    path_log_print(n);
    return 0;
}
