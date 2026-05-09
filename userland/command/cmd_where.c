#include "cmd_decl.h"
#include "path_log.h"
#include <stdio.h>
#include <stdlib.h>

int cmd_where_run(int argc, char **argv) {
    char **args = argv;
    int n = (argc >= 2) ? atoi(args[1]) : 16;
    path_log_print(n);
    return 0;
}
