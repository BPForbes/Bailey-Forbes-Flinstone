#include "common.h"
#include "cmd_decl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int cmd_exit_maybe(char *trimmed) {
    if (strncmp(trimmed, "exit", 4) != 0)
        return 0;
    char *tokens[3] = { NULL, NULL, NULL };
    int tcount = 0;
    char *tok = strtok(trimmed, " \t");
    while (tok && tcount < 3) {
        tokens[tcount++] = tok;
        tok = strtok(NULL, " \t");
    }
    if (tcount >= 2) {
        if (!strcmp(tokens[1], "-y") || !strcmp(tokens[1], "-Y")) {
            if (unlink(HISTORY_FILE) == 0)
                printf("History file deleted.\n");
            else
                perror("Failed to delete history file");
        } else if (!strcmp(tokens[1], "-n") || !strcmp(tokens[1], "-N")) {
            printf("History file retained.\n");
        } else {
            printf("Usage: exit [ -y | -n ]\n");
            return 1;
        }
        printf("Exiting shell...\n");
        shell_running = 0;
        exit(0);
    } else {
        if (!isatty(STDIN_FILENO))
            printf("History file retained.\n");
        else {
            char response[10];
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
        printf("Exiting shell...\n");
        shell_running = 0;
        exit(0);
    }
}
