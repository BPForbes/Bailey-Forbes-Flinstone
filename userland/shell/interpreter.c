#include "interpreter.h"
#include "common.h"
#include "util.h"
#include "terminal.h"
#include "cmd_decl.h"
#include "threadpool.h"
#include "shell_io.h"
#include "contract_p2_authz.h"
#include "contract_p2_principal_names.h"
#include "fl/audit_log.h"
#include "fl/authz_subsystem.h"
#include "fl/shell_authz.h"
#include "fl/session.h"
#include "cmd_authutil.h"
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

    if (strcmp(args[0], "sudo") == 0) {
        /* -i / -k: no password gate; cmd_sudo_run handles revoke and root login. */
        if (argc >= 2 && strcmp(args[1], "-k") == 0) {
            out_rc = cmd_sudo_run(argc, args);
            free(cmdLine);
            goto finish;
        }
        if (argc >= 2 && strcmp(args[1], "-i") == 0) {
            out_rc = cmd_sudo_run(argc, args);
            free(cmdLine);
            goto finish;
        }
        if (argc < 2) {
            out_rc = cmd_sudo_run(argc, args);
            free(cmdLine);
            goto finish;
        }
        if (cmd_sudo_require_auth() != 0) {
            free(cmdLine);
            out_rc = 1;
            goto finish;
        }
        if (argc == 2) {
            fl_shell_cmd_no_t sub = fl_shell_cmd_lookup(args[1]);
            if (sub != FL_SCMD_UNKNOWN && sub != FL_SCMD_SUDO) {
                fl_authz_decision_t sh = fl_shell_authz_builtin(sub, 1, &args[1]);
                if (sh == FL_AUTHZ_DENY) {
                    fl_audit_authz_event(line, (unsigned)sub, 1);
                    free(cmdLine);
                    out_rc = 1;
                    goto finish;
                }
                fl_audit_authz_event(line, (unsigned)sub, 0);
                fl_session_begin_sudo_scope();
                out_rc = fl_shell_cmd_dispatch(sub, 1, &args[1]);
                fl_session_end_sudo_scope();
                free(cmdLine);
                goto finish;
            }
            {
                fl_authz_decision_t fx = fl_shell_authz_foreign_exec(1, &args[1]);
                if (fx == FL_AUTHZ_DENY) {
                    fl_audit_authz_event(line, 0u, 1);
                    free(cmdLine);
                    out_rc = 1;
                    goto finish;
                }
                fl_audit_authz_event(line, (unsigned)FL_AUTHZ_OP_SHELL_FOREIGN_EXEC, 0);
                fl_session_begin_sudo_scope();
                {
                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork");
                        fl_session_end_sudo_scope();
                        free(cmdLine);
                        out_rc = 1;
                        goto finish;
                    }
                    if (pid == 0) {
                        signal(SIGINT, SIG_DFL);
                        execvp(args[1], &args[1]);
                        perror("execvp");
                        _exit(127);
                    }
                    {
                        int status = 0;
                        if (waitpid(pid, &status, 0) < 0)
                            out_rc = 1;
                        else if (WIFEXITED(status))
                            out_rc = WEXITSTATUS(status);
                        else if (WIFSIGNALED(status))
                            out_rc = 128 + WTERMSIG(status);
                        else
                            out_rc = 1;
                    }
                }
                fl_session_end_sudo_scope();
                free(cmdLine);
                goto finish;
            }
        }
        {
            int inner_argc = argc - 1;
            char **inner_argv = args + 1;
            fl_shell_cmd_no_t inner_id = fl_shell_cmd_lookup(inner_argv[0]);
            if (inner_id == FL_SCMD_SUDO) {
                out_rc = cmd_sudo_run(inner_argc, inner_argv);
                free(cmdLine);
                goto finish;
            }
            if (inner_id == FL_SCMD_UNKNOWN) {
                fl_authz_decision_t fx = fl_shell_authz_foreign_exec(inner_argc, inner_argv);
                if (fx == FL_AUTHZ_DENY) {
                    fl_audit_authz_event(line, 0u, 1);
                    free(cmdLine);
                    out_rc = 1;
                    goto finish;
                }
                fl_audit_authz_event(line, (unsigned)FL_AUTHZ_OP_SHELL_FOREIGN_EXEC, 0);
                fl_session_begin_sudo_scope();
                pid_t pid = fork();
                if (pid < 0) {
                    perror("fork");
                    fl_session_end_sudo_scope();
                    free(cmdLine);
                    out_rc = 1;
                    goto finish;
                }
                if (pid == 0) {
                    signal(SIGINT, SIG_DFL);
                    execvp(inner_argv[0], inner_argv);
                    perror("execvp");
                    _exit(127);
                }
                {
                    int status = 0;
                    if (waitpid(pid, &status, 0) < 0)
                        out_rc = 1;
                    else if (WIFEXITED(status))
                        out_rc = WEXITSTATUS(status);
                    else if (WIFSIGNALED(status))
                        out_rc = 128 + WTERMSIG(status);
                    else
                        out_rc = 1;
                }
                fl_session_end_sudo_scope();
                free(cmdLine);
                goto finish;
            }
            {
                fl_authz_decision_t sh = fl_shell_authz_builtin(inner_id, inner_argc, inner_argv);
                if (sh == FL_AUTHZ_DENY) {
                    fl_audit_authz_event(line, (unsigned)inner_id, 1);
                    free(cmdLine);
                    out_rc = 1;
                    goto finish;
                }
                fl_audit_authz_event(line, (unsigned)inner_id, 0);
                fl_session_begin_sudo_scope();
                {
                    unsigned sub_op = fl_authz_subsystem_op_for_shell_cmd((unsigned)inner_id);
                    if (sub_op != (unsigned)FL_AUTHZ_OP_UNSPECIFIED) {
                        fl_authz_decision_t sub =
                            fl_authz_subsystem_check(sub_op, NULL);
                        fl_audit_authz_event(line, sub_op, sub == FL_AUTHZ_DENY ? 1 : 0);
                        if (sub == FL_AUTHZ_DENY) {
                            fl_session_end_sudo_scope();
                            free(cmdLine);
                            out_rc = 1;
                            goto finish;
                        }
                    }
                }
                out_rc = fl_shell_cmd_dispatch(inner_id, inner_argc, inner_argv);
                fl_session_end_sudo_scope();
            }
            free(cmdLine);
            goto finish;
        }
    }

    if (strcmp(args[0], "su") == 0) {
        out_rc = cmd_su_run(argc, args);
        free(cmdLine);
        goto finish;
    }

    fl_shell_cmd_no_t id = fl_shell_cmd_lookup(args[0]);
    if (id == FL_SCMD_UNKNOWN) {
        fl_authz_decision_t fx = fl_shell_authz_foreign_exec(argc, args);
        if (fx == FL_AUTHZ_DENY) {
            fl_audit_authz_event(line, 0u, 1);
            free(cmdLine);
            out_rc = 1;
            goto finish;
        }
        fl_audit_authz_event(line, (unsigned)FL_AUTHZ_OP_SHELL_FOREIGN_EXEC, 0);
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
            if (waitpid(pid, &status, 0) < 0)
                out_rc = 1;
            else if (WIFEXITED(status))
                out_rc = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                out_rc = 128 + WTERMSIG(status);
            else
                out_rc = 1;
            free(cmdLine);
            goto finish;
        }
    }

    fl_authz_decision_t sh = fl_shell_authz_builtin(id, argc, args);
    if (sh == FL_AUTHZ_DENY) {
        fl_audit_authz_event(line, (unsigned)id, 1);
        free(cmdLine);
        out_rc = 1;
        goto finish;
    }
    fl_audit_authz_event(line, (unsigned)id, 0);

    {
        unsigned sub_op = fl_authz_subsystem_op_for_shell_cmd((unsigned)id);
        if (sub_op != (unsigned)FL_AUTHZ_OP_UNSPECIFIED) {
            fl_authz_decision_t sub = fl_authz_subsystem_check(sub_op, NULL);
            fl_audit_authz_event(line, sub_op, sub == FL_AUTHZ_DENY ? 1 : 0);
            if (sub == FL_AUTHZ_DENY) {
                free(cmdLine);
                out_rc = 1;
                goto finish;
            }
        }
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
    /* getenv is not async-signal-safe; concurrent setenv is not expected for
     * FL_PRINCIPAL_ENV_NAME in normal shell or CUnit runs (single-threaded tests). */
    const char *p = getenv(FL_PRINCIPAL_ENV_NAME);
    return p && strcmp(p, FL_PRINCIPAL_GUEST_LITERAL) == 0;
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
    return fl_authz_guest_shell_builtin((unsigned)no);
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
    printf("Press Ctrl+C (^C) to cancel input or interrupt a running command.\n");
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
        fl_shell_io_set_prompt_active(1, line, len, pos);
        while (1) {
            char c;
            ssize_t nread = read(STDIN_FILENO, &c, 1);
            if (nread <= 0)
                break;
            if (c == '\r' || c == '\n') {
                write(STDOUT_FILENO, "\n", 1);
                fl_shell_io_set_prompt_active(0, NULL, 0, 0);
                break;
            } else if ((unsigned char)c == 3) {
                /* Ctrl+C: discard the current input line and return to a fresh prompt. */
                write(STDOUT_FILENO, "^C\n", 3);
                len = 0;
                pos = 0;
                line[0] = '\0';
                fl_shell_io_set_prompt_active(0, NULL, 0, 0);
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
            /* Refresh the snapshot used by shell_io for prompt-aware async
             * prints (background server / client events). */
            fl_shell_io_set_prompt_active(1, line, len, pos);
        }
        disable_raw_mode();
        if (len > 0) {
            submit_single_command_interruptible(line);
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
