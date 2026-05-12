#include "cmd_decl.h"
#include "common.h"
#include "fl/audit_log.h"
#include "fs_jail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_audit_help(void) {
    printf(
        "Usage: audit [show [N]] | path | --help\n"
        "\n"
        "Inspect the **audit log** (separate from command history). Lines are written\n"
        "automatically after each shell command completes when **%s** is set\n"
        "(non-empty, not `0`). Default file: **%s** in the process cwd (inside the VM\n"
        "host sandbox when jail mode is active; see fl/jail_contract.h).\n"
        "\n"
        "Commands:\n"
        "  show [N]   Print the last N lines (default 32, max 10000)\n"
        "  path       Show relative log path and whether the fs jail is active\n"
        "  --help     Show this message\n",
        FL_AUDIT_ENV, FL_AUDIT_REL_DEFAULT);
}

int cmd_audit_run(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            print_audit_help();
            return 0;
        }
    }

    if (argc >= 2 && !strcmp(argv[1], "path")) {
        printf("audit_log_relative=%s\n", FL_AUDIT_REL_DEFAULT);
        printf("jail_active=%d\n", fs_jail_is_active());
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "show")) {
        int n = 32;
        if (argc >= 3) {
            char *endptr;
            long val = strtol(argv[2], &endptr, 10);
            if (*endptr != '\0' || endptr == argv[2]) {
                fprintf(stderr, "audit: invalid number '%s'\n", argv[2]);
                return 1;
            }
            if (val < 0 || val > 10000) {
                fprintf(stderr, "audit: N must be in range 0..10000\n");
                return 1;
            }
            n = (int)val;
        }
        return fl_audit_show_last_lines(n);
    }

    if (argc == 1) {
        print_audit_help();
        return 0;
    }

    fprintf(stderr, "audit: unknown subcommand; try `audit --help`\n");
    return 1;
}
