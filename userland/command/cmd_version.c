#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
        return 0;
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
    return 0;
}
