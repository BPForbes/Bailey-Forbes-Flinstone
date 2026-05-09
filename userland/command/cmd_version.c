#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * Handle the `version` command: show shell version and optionally delete the history file.
 *
 * When invoked with `-y`/`-Y` attempts to delete HISTORY_FILE and reports success or errno
 * on failure; with `-n`/`-N` preserves the history. With no flag, if stdin is a TTY the
 * function prompts the user to confirm deletion, otherwise it preserves the history.
 * On successful/non-usage-error paths the function prints the shell version and terminates
 * the process with exit status 0.
 *
 * @param argc Number of command-line arguments.
 * @param argv Argument vector; argv[1] (if present) may be "-y"/"-Y" or "-n"/"-N".
 * @returns `1` if an invalid flag is provided (usage error). On other code paths the
 *          function does not return because it calls exit(0).
 */
int cmd_version_run(int argc, char **argv) {
    char **args = argv;
    if (argc >= 2) {
        if (!strcmp(args[1], "-y") || !strcmp(args[1], "-Y")) {
            if (unlink(HISTORY_FILE) == 0)
                printf("History file deleted.\n");
            else
                perror("Failed to delete history file");
        } else if (!strcmp(args[1], "-n") || !strcmp(args[1], "-N")) {
            printf("History file retained.\n");
        } else {
            printf("Usage: version [ -y | -n ]\n");
            return 1;
        }
        printf("Shell version: %s\n", VERSION);
        exit(0);
    }
    if (!isatty(STDIN_FILENO))
        printf("History file retained.\n");
    else {
        char response[10];
        printf("Shell version: %s\n", VERSION);
        printf("Do you want to delete your shell history? [y/n]: ");
        fflush(stdout);
        if (fgets(response, sizeof(response), stdin)) {
            response[strcspn(response, "\n")] = '\0';
            if (!strcasecmp(response, "y")) {
                if (unlink(HISTORY_FILE) == 0)
                    printf("History file deleted.\n");
                else
                    perror("Failed to delete history file");
            } else {
                printf("History file retained.\n");
            }
        }
    }
    exit(0);
}
