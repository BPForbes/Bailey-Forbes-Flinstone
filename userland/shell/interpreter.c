#include "interpreter.h"
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "cmd_decl.h"
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

/*
 * execute_command_str:
 *   Parses and executes the command provided in 'line'.
 *   Returns 0 on success; otherwise a nonzero value.
 *
 * Builtins are dispatched by numeric command id (see userland/command/fl_shell_cmd.h),
 * analogous to fl_syscall_dispatch() for kernel syscalls.
 */
int execute_command_str(const char *line) {
    if (!line || !*line)
        return 0;
    append_history(line);
    char buffer[512];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    char *tokenBuf = strdup(buffer);
    char *trimmed = trim_whitespace(tokenBuf);

    if (cmd_exit_maybe(trimmed)) {
        free(tokenBuf);
        return 1;
    }
    if (cmd_clear_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }
    if (cmd_help_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }
    if (cmd_history_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }
    if (cmd_cc_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }
    if (cmd_bios_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }
    if (cmd_make_maybe(trimmed)) {
        free(tokenBuf);
        return 0;
    }

    char *cmdLine = strdup(buffer);
    char *args[64];
    int argc = 0;
    char *t = strtok(cmdLine, " \t");
    while (t && argc < 63) {
        args[argc++] = t;
        t = strtok(NULL, " \t");
    }
    args[argc] = NULL;
    free(tokenBuf);
    if (argc == 0) {
        free(cmdLine);
        return 0;
    }

    fl_shell_cmd_no_t id = fl_shell_cmd_lookup(args[0]);
    if (id == FL_SCMD_UNKNOWN) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(cmdLine);
            return 1;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            execvp(args[0], args);
            perror("execvp");
            _exit(127);
        } else {
            int status = 0;
            waitpid(pid, &status, 0);
            free(cmdLine);
            return WEXITSTATUS(status);
        }
    }

    int rc = fl_shell_cmd_dispatch(id, argc, args);
    free(cmdLine);
    return rc;
}

/* ---------------------------------------------------------------------------
 * Interactive Shell
 *
 * When UNIT_TEST is defined the interactive shell is simulated so that tests do not block.
 * -------------------------------------------------------------------------*/
#ifdef UNIT_TEST
void interactive_shell(void) {
    printf("[UNIT_TEST] Interactive shell skipped.\n");
    return;
}
#else
void interactive_shell(void) {
    printf("[INTERACTIVE MODE] Type 'exit' to leave interactive mode.\n");
    char line[1024] = {0};
    int len = 0;
    g_interactive_history = load_history(&g_interactive_history_count);
    if (!g_interactive_history) {
        g_interactive_history = malloc(sizeof(char *));
        g_interactive_history_count = 0;
    }
    int currHistIndex = g_interactive_history_count;
    enable_raw_mode();
    while (shell_running) {
        if (g_history_cleared) {
            if (g_interactive_history)
                free_history(g_interactive_history, g_interactive_history_count);
            g_interactive_history = malloc(sizeof(char *));
            g_interactive_history_count = 0;
            g_history_cleared = 0;
            currHistIndex = 0;
        }
        printf("shell> ");
        fflush(stdout);
        len = 0;
        memset(line, 0, sizeof(line));
        currHistIndex = g_interactive_history_count;
        while (1) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread <= 0)
                break;
            if (c == '\r' || c == '\n') {
                write(STDOUT_FILENO, "\n", 1);
                break;
            } else if ((unsigned char)c == 127 || (unsigned char)c == '\b') {
                /* DEL (127) or BS (8): terminals differ; BS alone moves the cursor
                 * without erasing, which used to corrupt the buffer and overwrite "shell>". */
                if (len > 0) {
                    len--;
                    line[len] = '\0';
                    write(STDOUT_FILENO, "\b \b", 3);
                }
            } else if (c == 27) {
                char seq[2];
                if (read(STDIN_FILENO, seq, 2) == 2) {
                    if (seq[0] == '[') {
                        if (seq[1] == 'A') {
                            if (currHistIndex > 0) {
                                currHistIndex--;
                                printf("\r\33[2Kshell> %s", g_interactive_history[currHistIndex]);
                                fflush(stdout);
                                strncpy(line, g_interactive_history[currHistIndex], sizeof(line) - 1);
                                line[sizeof(line) - 1] = '\0';
                                len = strlen(line);
                            }
                        } else if (seq[1] == 'B') {
                            if (currHistIndex < g_interactive_history_count - 1) {
                                currHistIndex++;
                                printf("\r\33[2Kshell> %s", g_interactive_history[currHistIndex]);
                                fflush(stdout);
                                strncpy(line, g_interactive_history[currHistIndex], sizeof(line) - 1);
                                line[sizeof(line) - 1] = '\0';
                                len = strlen(line);
                            } else {
                                currHistIndex = g_interactive_history_count;
                                printf("\r\33[2Kshell> ");
                                fflush(stdout);
                                len = 0;
                                line[0] = '\0';
                            }
                        }
                    }
                }
            } else {
                if (len < (int)sizeof(line) - 1) {
                    line[len++] = c;
                    write(STDOUT_FILENO, &c, 1);
                }
            }
        }
        disable_raw_mode();
        if (len > 0) {
            submit_single_command(line);
            char **tmp_hist = realloc(g_interactive_history, sizeof(char *) * (g_interactive_history_count + 2));
            if (tmp_hist) {
                g_interactive_history = tmp_hist;
                char *new_entry = strdup(line);
                if (new_entry) {
                    g_interactive_history[g_interactive_history_count] = new_entry;
                    g_interactive_history_count++;
                }
            }
            enable_raw_mode();
        }
    }
    disable_raw_mode();
    free_history(g_interactive_history, g_interactive_history_count);
}
#endif
