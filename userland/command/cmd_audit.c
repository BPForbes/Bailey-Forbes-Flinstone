#include "cmd_decl.h"
#include "common.h"
#include "fl/audit_log.h"
#include "contract_p6_audit_trail.h"
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
        "  show [N]   Print the last N lines (default %d, N in 1..%u). For very\n"
        "             large logs, only the last 2 MiB is scanned for the tail, so\n"
        "             fewer than N lines may appear if newlines are sparse.\n"
        "  ring       Print the in-memory ring buffer (drops counter); see **P6-2**.\n"
        "  path       Show relative log path and whether the fs jail is active\n"
        "  --help     Show this message\n",
        FL_AUDIT_ENV, FL_AUDIT_REL_DEFAULT,
        FL_CONTRACT_P6_AUDIT_SHOW_N_DEFAULT, FL_CONTRACT_P6_AUDIT_SHOW_N_MAX);
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

    if (argc >= 2 && !strcmp(argv[1], "ring")) {
        unsigned d = fl_ring_log_drop_count();
        char buf[FL_RING_LOG_CAPACITY + 2u];
        size_t n = fl_ring_log_copy_out(buf, sizeof buf);
        printf("ring_drops=%u\n", d);
        if (n == 0u)
            printf("(ring empty)\n");
        else
            fputs(buf, stdout);
        return 0;
    }

    if (argc >= 2 && !strcmp(argv[1], "show")) {
        int n = (int)FL_CONTRACT_P6_AUDIT_SHOW_N_DEFAULT;
        if (argc >= 3) {
            char *endptr;
            long val = strtol(argv[2], &endptr, 10);
            if (*endptr != '\0' || endptr == argv[2]) {
                fprintf(stderr, "audit: invalid number '%s'\n", argv[2]);
                return 1;
            }
            if (val <= 0 || val > (long)FL_CONTRACT_P6_AUDIT_SHOW_N_MAX) {
                fprintf(stderr, "audit: N must be in range 1..%u\n",
                        (unsigned)FL_CONTRACT_P6_AUDIT_SHOW_N_MAX);
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
