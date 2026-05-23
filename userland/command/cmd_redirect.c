#include "common.h"
#include "cmd_decl.h"
#include "cmd_batch.h"
#include "cmd_util.h"
#include "fs.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

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

int cmd_redirect_batch_tokens_count(int argc, char **argv, int i) {
    (void)argc; (void)argv; (void)i; return 2;
}
