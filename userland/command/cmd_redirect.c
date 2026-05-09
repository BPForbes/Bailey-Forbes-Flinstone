#include "common.h"
#include "cmd_decl.h"
#include "cmd_util.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

/**
 * Configure output redirection using the first command argument.
 *
 * If the first argument is "off", disable redirection; otherwise enable
 * redirection to the resolved path of the provided filename. Prints a usage
 * message and fails when insufficient arguments are supplied. If the resolved
 * path is blocked by the jail check, the command aborts with an error.
 *
 * @param argc Number of arguments in `argv`.
 * @param argv Argument vector; `argv[1]` is "off" to disable redirection or a
 *             filename to which output should be redirected.
 * @returns `0` on success, `1` on failure (insufficient arguments or blocked path).
 */
int cmd_redirect_run(int argc, char **argv) {
    char **args = argv;
    if (argc < 2) {
        printf("Usage: redirect <filename> or \"redirect off\"\n");
        return 1;
    }
    if (!strcmp(args[1], "off")) {
        do_redirect_output(args[1]);
    } else {
        char rpath[CWD_MAX];
        resolve_path(args[1], rpath, sizeof(rpath));
        if (cmd_jail_blocked_path("redirect", args[1], rpath))
            return 1;
        do_redirect_output(rpath);
    }
    return 0;
}
