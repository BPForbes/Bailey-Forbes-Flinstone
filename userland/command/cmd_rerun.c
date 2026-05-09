#include "cmd_decl.h"
#include "interpreter.h"
#include "util.h"
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * Re-executes a previously saved command from the history by numeric index.
 *
 * If called with fewer than 2 arguments, prints a usage message. If the
 * provided index is not greater than zero or the history line does not
 * exist, prints an explanatory error message. On success the retrieved
 * command is printed and submitted for execution.
 *
 * @param argc Number of arguments; expects at least 2 (command name and index).
 * @param argv Argument vector; argv[1] should be the history index to re-run.
 * @returns 0 on successful submission of the history command, 1 on error.
 */
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
    submit_single_command(cmd);
    free(cmd);
    return 0;
}
