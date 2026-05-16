#include "interpreter.h"
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "cmd_decl.h"
#include "threadpool.h"
#include "fl/audit_log.h"
#include "fl/shell_authz.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

/*
 * execute_command_str:
 *   Parses and executes the command provided in 'line'.
 *   Returns 0 on success; otherwise a nonzero value.
 *
 * Builtins are dispatched by numeric command id (see userland/command/fl_shell_cmd.h),
 * analogous to fl_syscall_dispatch() for kernel syscalls.
 */
int execute_command_str(const char *line) {
    int out_rc = 0;
    if (!line || !*line)
        goto finish;

    append_history(line);

    char buffer[512];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    char *tokenBuf = strdup(buffer);
    if (!tokenBuf)
        goto finish;
    char *trimmed = trim_whitespace(tokenBuf);

    if (cmd_exit_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 1;
        goto finish;
    }
    if (cmd_clear_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }
    if (cmd_help_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }
    if (cmd_history_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }
    if (cmd_cc_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }
    if (cmd_bios_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }
    if (cmd_make_maybe(trimmed)) {
        free(tokenBuf);
        out_rc = 0;
        goto finish;
    }

    char *cmdLine = strdup(buffer);
    if (!cmdLine) {
        free(tokenBuf);
        goto finish;
    }
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
        goto finish;
    }

    fl_shell_cmd_no_t id = fl_shell_cmd_lookup(args[0]);
    if (id == FL_SCMD_UNKNOWN) {
        if (fl_shell_authz_foreign_exec(argc, args) == FL_AUTHZ_DENY) {
            fl_audit_authz_event(line, 0u, 1);
            free(cmdLine);
            out_rc = 1;
            goto finish;
        }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            free(cmdLine);
            out_rc = 1;
            goto finish;
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
            out_rc = WEXITSTATUS(status);
            goto finish;
        }
    }

    if (fl_shell_authz_builtin(id, argc, args) == FL_AUTHZ_DENY) {
        fl_audit_authz_event(line, (unsigned)id, 1);
        free(cmdLine);
        out_rc = 1;
        goto finish;
    }

    out_rc = fl_shell_cmd_dispatch(id, argc, args);
    free(cmdLine);

finish:
    fl_audit_shell_completed(line, out_rc);
    return out_rc;
}

/* ---------------------------------------------------------------------------
 * Shell authorization (**P2-3**) — same TU as the interpreter to avoid a
 * separate userland object file; policy is hosted-only (getenv / hook).
 * -------------------------------------------------------------------------*/
static fl_shell_authz_hook_fn s_shell_authz_hook;
static void *s_shell_authz_hook_ctx;
static pthread_mutex_t s_shell_authz_mu = PTHREAD_MUTEX_INITIALIZER;

void fl_shell_authz_set_hook(fl_shell_authz_hook_fn hook_fn, void *ctx) {
    pthread_mutex_lock(&s_shell_authz_mu);
    s_shell_authz_hook = hook_fn;
    s_shell_authz_hook_ctx = ctx;
    pthread_mutex_unlock(&s_shell_authz_mu);
}

static int principal_is_guest(void) {
    const char *p = getenv("FL_PRINCIPAL");
    return p && strcmp(p, "guest") == 0;
}

static fl_authz_decision_t guest_builtin_policy(fl_shell_cmd_no_t no) {
    switch (no) {
    case FL_SCMD_FORMAT:
    case FL_SCMD_SETDISK:
    case FL_SCMD_INITDISK:
    case FL_SCMD_CREATEDISK:
    case FL_SCMD_RMTREE:
    case FL_SCMD_DISKPUT:
    case FL_SCMD_DISKGET:
    case FL_SCMD_DISKDEL:
    case FL_SCMD_DISKMKDIR:
    case FL_SCMD_REDIRECT:
    case FL_SCMD_IMPORT:
    case FL_SCMD_WRITE:
    case FL_SCMD_WRITECLUSTER:
    case FL_SCMD_DELCLUSTER:
    case FL_SCMD_UPDATE:
    case FL_SCMD_ADDCLUSTER:
        return FL_AUTHZ_DENY;
    default:
        return FL_AUTHZ_ALLOW;
    }
}

fl_authz_decision_t fl_shell_authz_builtin(fl_shell_cmd_no_t no, int argc, char **argv) {
    fl_shell_authz_hook_fn hook = NULL;
    void *hook_ctx = NULL;
    pthread_mutex_lock(&s_shell_authz_mu);
    hook = s_shell_authz_hook;
    hook_ctx = s_shell_authz_hook_ctx;
    pthread_mutex_unlock(&s_shell_authz_mu);
    if (hook) {
        return hook(no, argc, argv, hook_ctx);
    }
    if (!principal_is_guest())
        return FL_AUTHZ_ALLOW;
    return guest_builtin_policy(no);
}

fl_authz_decision_t fl_shell_authz_foreign_exec(int argc, char **argv) {
    fl_shell_authz_hook_fn hook = NULL;
    void *hook_ctx = NULL;
    pthread_mutex_lock(&s_shell_authz_mu);
    hook = s_shell_authz_hook;
    hook_ctx = s_shell_authz_hook_ctx;
    pthread_mutex_unlock(&s_shell_authz_mu);
    if (hook) {
        return hook(FL_SCMD_UNKNOWN, argc, argv, hook_ctx);
    }
    if (!principal_is_guest())
        return FL_AUTHZ_ALLOW;
    return FL_AUTHZ_DENY;
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
    int pos = 0; /* cursor: index in [0, len] */
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
        pos = 0;
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
                /* DEL (127) or BS (8): delete before cursor; redraw so the prompt stays consistent. */
                if (pos > 0) {
                    memmove(line + pos - 1, line + pos, (size_t)(len - pos) + 1u);
                    pos--;
                    len--;
                    printf("\r\33[2Kshell> ");
                    if (len > 0)
                        fwrite(line, 1, (size_t)len, stdout);
                    fflush(stdout);
                    if (pos < len) {
                        int back = len - pos;
                        if (back > 0 && back < 10000)
                            printf("\033[%dD", back);
                        fflush(stdout);
                    }
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
                                len = (int)strlen(line);
                                pos = len;
                            }
                        } else if (seq[1] == 'B') {
                            if (currHistIndex < g_interactive_history_count - 1) {
                                currHistIndex++;
                                printf("\r\33[2Kshell> %s", g_interactive_history[currHistIndex]);
                                fflush(stdout);
                                strncpy(line, g_interactive_history[currHistIndex], sizeof(line) - 1);
                                line[sizeof(line) - 1] = '\0';
                                len = (int)strlen(line);
                                pos = len;
                            } else {
                                currHistIndex = g_interactive_history_count;
                                printf("\r\33[2Kshell> ");
                                fflush(stdout);
                                len = 0;
                                pos = 0;
                                line[0] = '\0';
                            }
                        } else if (seq[1] == 'C') {
                            /* right arrow */
                            if (pos < len) {
                                pos++;
                                write(STDOUT_FILENO, "\033[C", 3);
                                fflush(stdout);
                            }
                        } else if (seq[1] == 'D') {
                            /* left arrow */
                            if (pos > 0) {
                                pos--;
                                write(STDOUT_FILENO, "\033[D", 3);
                                fflush(stdout);
                            }
                        }
                    }
                }
            } else {
                if (c == '\t' || ((unsigned char)c >= 32 && (unsigned char)c != 127)) {
                    if (len < (int)sizeof(line) - 1) {
                        memmove(line + pos + 1, line + pos, (size_t)(len - pos) + 1u);
                        line[pos] = c;
                        pos++;
                        len++;
                        printf("\r\33[2Kshell> ");
                        if (len > 0)
                            fwrite(line, 1, (size_t)len, stdout);
                        fflush(stdout);
                        if (pos < len) {
                            int back = len - pos;
                            if (back > 0 && back < 10000)
                                printf("\033[%dD", back);
                            fflush(stdout);
                        }
                    }
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
