/**
 * test_audit_log.c — Unit tests for the audit log subsystem (userland/shell/audit_log.c)
 * and the fl_log_sink_t / fl_authz contract types introduced in this PR.
 *
 * Tests cover:
 *   fl_audit_show_last_lines(): missing file, empty file, n-clamping,
 *       writing known lines and reading back last N
 *   fl_audit_shell_completed(): no-op when FL_AUDIT unset, no-op when cmd==NULL,
 *       writes record when FL_AUDIT=1
 *   fl_audit_set_sink(): sink emit callback is invoked on audit event
 *   fl_log_sink_t / fl_log_level_t values
 *   fl_authz_decision_t values and fl_authz_check_fn callable
 */

#include "fl/audit_log.h"
#include "fl/contract.h"
#include "fl/contract_log_dispatch.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* POSIX temp dir helpers */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define ASSERT(c) do { \
    if (!(c)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #c); \
        return 1; \
    } \
} while(0)

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static char g_tmpdir[PATH_MAX];

static int make_tmpdir(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !base[0])
        base = "/tmp";
    snprintf(g_tmpdir, sizeof g_tmpdir, "%s/fl_audit_test_XXXXXX", base);
    if (!mkdtemp(g_tmpdir)) {
        perror("mkdtemp");
        return -1;
    }
    return 0;
}

static void rmrf_dir(const char *path) {
    /* Simple: remove the audit log file and then rmdir */
    char audit_path[PATH_MAX];
    snprintf(audit_path, sizeof audit_path, "%s/%s", path, FL_AUDIT_REL_DEFAULT);
    unlink(audit_path);
    rmdir(path);
}

/* Helper: change to tmpdir and back */
static char g_saved_cwd[PATH_MAX];

static int enter_tmpdir(void) {
    if (!getcwd(g_saved_cwd, sizeof g_saved_cwd)) {
        perror("getcwd");
        return -1;
    }
    if (chdir(g_tmpdir) != 0) {
        perror("chdir tmpdir");
        return -1;
    }
    return 0;
}

static void leave_tmpdir(void) {
    (void)chdir(g_saved_cwd);
}

/* Write lines to the audit log file directly (bypassing env check) */
static int write_audit_lines(const char **lines, int count) {
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "w");
    if (!fp)
        return -1;
    for (int i = 0; i < count; i++) {
        if (fprintf(fp, "%s\n", lines[i]) < 0) {
            fclose(fp);
            return -1;
        }
    }
    return fclose(fp) == 0 ? 0 : -1;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_show_last_lines()
 * ---------------------------------------------------------------------- */

static int test_audit_show_no_file(void) {
    ASSERT(enter_tmpdir() == 0);
    /* Ensure the audit log does not exist */
    unlink(FL_AUDIT_REL_DEFAULT);

    /* Should return 0 (not an error) and print a "not found" message */
    int rc = fl_audit_show_last_lines(5);
    ASSERT(rc == 0);

    leave_tmpdir();
    return 0;
}

static int test_audit_show_empty_file(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Create an empty audit log */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "w");
    ASSERT(fp != NULL);
    fclose(fp);

    /* Should return 0 and print "(audit log empty)" */
    int rc = fl_audit_show_last_lines(10);
    ASSERT(rc == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_show_n_clamping(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Write a few lines so the file is non-empty */
    const char *lines[] = { "line1", "line2" };
    ASSERT(write_audit_lines(lines, 2) == 0);

    /* n <= 0 should be clamped to 32 (no crash) */
    ASSERT(fl_audit_show_last_lines(0) == 0);
    ASSERT(fl_audit_show_last_lines(-1) == 0);

    /* n > 10000 should be clamped to 32 (no crash) */
    ASSERT(fl_audit_show_last_lines(10001) == 0);
    ASSERT(fl_audit_show_last_lines(99999) == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_show_returns_last_n(void) {
    ASSERT(enter_tmpdir() == 0);

    /* Write 10 distinct lines */
    const char *lines[] = {
        "alpha", "bravo", "charlie", "delta", "echo",
        "foxtrot", "golf", "hotel", "india", "juliet",
    };
    int total = (int)(sizeof lines / sizeof lines[0]);
    ASSERT(write_audit_lines(lines, total) == 0);

    /* Ask for last 3 — should succeed without error */
    ASSERT(fl_audit_show_last_lines(3) == 0);

    /* Ask for more lines than exist — should succeed */
    ASSERT(fl_audit_show_last_lines(100) == 0);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_shell_completed()
 * ---------------------------------------------------------------------- */

static int test_audit_completed_noop_when_env_unset(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    /* Ensure FL_AUDIT is unset */
    unsetenv(FL_AUDIT_ENV);
    fl_audit_shell_completed("echo hello", 0);

    /* File must NOT have been created */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    leave_tmpdir();
    return 0;
}

static int test_audit_completed_noop_env_zero(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "0", 1);
    fl_audit_shell_completed("echo hello", 0);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    unsetenv(FL_AUDIT_ENV);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_noop_null_cmd(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    /* NULL cmd must not crash */
    fl_audit_shell_completed(NULL, 0);

    /* File must NOT have been created */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp == NULL);

    unsetenv(FL_AUDIT_ENV);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_writes_file(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("ls /tmp", 0);
    unsetenv(FL_AUDIT_ENV);

    /* File must have been created and be non-empty */
    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* Record must contain expected fields */
    ASSERT(strstr(buf, "type=shell") != NULL);
    ASSERT(strstr(buf, "host_rc=0") != NULL);
    ASSERT(strstr(buf, "fl_rc=0") != NULL);
    ASSERT(strstr(buf, "cmd=ls /tmp") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_nonzero_exit_maps_to_err(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("badcmd", 1);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* Non-zero host_rc → fl_rc maps to FL_RESULT_ERR (-1) */
    ASSERT(strstr(buf, "host_rc=1") != NULL);
    ASSERT(strstr(buf, "fl_rc=-1") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_completed_sanitizes_control_chars(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    setenv(FL_AUDIT_ENV, "1", 1);
    /* cmd with control chars and quotes that should be sanitized */
    fl_audit_shell_completed("cmd\x01with\"quotes\\slash", 0);
    unsetenv(FL_AUDIT_ENV);

    FILE *fp = fopen(FL_AUDIT_REL_DEFAULT, "r");
    ASSERT(fp != NULL);
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof buf - 1u, fp);
    ASSERT(nr > 0u);
    buf[nr] = '\0';
    fclose(fp);

    /* The cmd= portion must not contain raw control chars, backslash, or double-quote */
    const char *cmd_field = strstr(buf, "cmd=");
    ASSERT(cmd_field != NULL);
    for (size_t i = 4; cmd_field[i] && cmd_field[i] != '\n'; i++) {
        unsigned char c = (unsigned char)cmd_field[i];
        ASSERT(c >= 32u);        /* no control chars */
        ASSERT(c != (unsigned char)'"');
        ASSERT(c != (unsigned char)'\\');
    }

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_audit_set_sink() / fl_log_sink_t
 * ---------------------------------------------------------------------- */

static int g_emit_called = 0;
static int g_emit_level = -1;
static char g_emit_msg[1024];

static void test_emit_fn(struct fl_log_sink *sink, int level,
                         int facility, const char *msg) {
    (void)sink;
    (void)facility;
    g_emit_called++;
    g_emit_level = level;
    if (msg)
        strncpy(g_emit_msg, msg, sizeof g_emit_msg - 1u);
}

static int test_audit_sink_invoked(void) {
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = NULL };
    fl_audit_set_sink(&sink);

    g_emit_called = 0;
    g_emit_level = -1;
    g_emit_msg[0] = '\0';

    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("ping test", 0);
    unsetenv(FL_AUDIT_ENV);

    /* Reset sink to avoid dangling pointer after this test */
    fl_audit_set_sink(NULL);

    ASSERT(g_emit_called >= 1);
    ASSERT(g_emit_level == (int)FL_LOG_INFO);
    ASSERT(strstr(g_emit_msg, "type=shell") != NULL);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

static int test_audit_sink_null_safe(void) {
    /* Setting sink to NULL and completing a command must not crash */
    ASSERT(enter_tmpdir() == 0);
    unlink(FL_AUDIT_REL_DEFAULT);

    fl_audit_set_sink(NULL);
    setenv(FL_AUDIT_ENV, "1", 1);
    fl_audit_shell_completed("echo ok", 0);
    unsetenv(FL_AUDIT_ENV);

    unlink(FL_AUDIT_REL_DEFAULT);
    leave_tmpdir();
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_level_t values
 * ---------------------------------------------------------------------- */

static int test_log_level_values(void) {
    ASSERT((int)FL_LOG_EMERG   == 0);
    ASSERT((int)FL_LOG_ALERT   == 1);
    ASSERT((int)FL_LOG_CRIT    == 2);
    ASSERT((int)FL_LOG_ERR     == 3);
    ASSERT((int)FL_LOG_WARNING == 4);
    ASSERT((int)FL_LOG_NOTICE  == 5);
    ASSERT((int)FL_LOG_INFO    == 6);
    ASSERT((int)FL_LOG_DEBUG   == 7);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_authz_decision_t and fl_authz_check_fn
 * ---------------------------------------------------------------------- */

static fl_authz_decision_t always_allow_fn(unsigned op, void *ctx) {
    (void)op; (void)ctx;
    return FL_AUTHZ_ALLOW;
}

static fl_authz_decision_t always_deny_fn(unsigned op, void *ctx) {
    (void)op; (void)ctx;
    return FL_AUTHZ_DENY;
}

static int test_authz_decisions(void) {
    ASSERT((int)FL_AUTHZ_DENY  == 0);
    ASSERT((int)FL_AUTHZ_ALLOW == 1);

    fl_authz_check_fn allow_fn = always_allow_fn;
    fl_authz_check_fn deny_fn  = always_deny_fn;

    ASSERT(allow_fn(0u, NULL) == FL_AUTHZ_ALLOW);
    ASSERT(deny_fn(0u,  NULL) == FL_AUTHZ_DENY);
    ASSERT(allow_fn(99u, (void *)0x1) == FL_AUTHZ_ALLOW);
    ASSERT(deny_fn(99u,  (void *)0x1) == FL_AUTHZ_DENY);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_sink_t struct layout (compile-time sanity)
 * ---------------------------------------------------------------------- */

static int test_log_sink_ops_struct(void) {
    /* Verify fl_log_sink_t can be constructed and ops pointer used safely */
    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = (void *)0xDEAD };

    ASSERT(sink.ops == &ops);
    ASSERT(sink.ops->emit == test_emit_fn);
    ASSERT(sink.impl == (void *)0xDEAD);

    /* Calling emit via the ops vtable must work */
    g_emit_called = 0;
    sink.ops->emit(&sink, (int)FL_LOG_WARNING, 1, "test-msg");
    ASSERT(g_emit_called == 1);
    ASSERT(g_emit_level == (int)FL_LOG_WARNING);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: fl_log_sink_emit_line dispatch + rate limit (ASM-backed buffer)
 * ---------------------------------------------------------------------- */

static int test_log_dispatch_rate_limit(void) {
    static const fl_log_sink_ops_t ops = { .emit = test_emit_fn };
    fl_log_sink_t sink = { .ops = &ops, .impl = NULL };

    fl_log_rate_limit_reset_for_tests();
    for (int i = 0; i < FL_LOG_RL_MAX_PER_SEC; i++) {
        g_emit_called = 0;
        ASSERT(fl_log_sink_emit_line(&sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "x") ==
               FL_RESULT_OK);
        ASSERT(g_emit_called == 1);
    }

    /*
     * The (FL_LOG_RL_MAX_PER_SEC + 1)th fl_log_sink_emit_line may land in a new monotonic
     * second; poll fl_log_sink_emit_line in a tight bounded loop (referencing
     * FL_RESULT_OK / FL_RESULT_ERR / g_emit_called / &sink) until FL_RESULT_ERR (expected
     * rate limit) — at most two full per-second windows need ~2 * FL_LOG_RL_MAX_PER_SEC + 1
     * OK emits before the next line must be rejected.
     */
    int saw_rate_limit = 0;
    const int max_tight = 2 * FL_LOG_RL_MAX_PER_SEC + 32;
    for (int k = 0; k < max_tight; k++) {
        g_emit_called = 0;
        fl_result_t rc =
            fl_log_sink_emit_line(&sink, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "overflow");
        if (rc == FL_RESULT_ERR) {
            ASSERT(g_emit_called == 0);
            saw_rate_limit = 1;
            break;
        }
        if (rc != FL_RESULT_OK) {
            fprintf(stderr, "FAIL [%s:%d]: unexpected fl_log_sink_emit_line return %d\n",
                    __FILE__, __LINE__, (int)rc);
            return 1;
        }
        ASSERT(g_emit_called == 1);
    }
    ASSERT(saw_rate_limit);
    return 0;
}

static int test_log_dispatch_null_sink(void) {
    ASSERT(fl_log_sink_emit_line(NULL, (int)FL_LOG_INFO, FL_LOG_FACILITY_AUDIT, "nope") ==
           FL_RESULT_INVAL);
    return 0;
}

/* -------------------------------------------------------------------------
 * Tests: audit constants (FL_AUDIT_REL_DEFAULT, FL_AUDIT_ENV)
 * ---------------------------------------------------------------------- */

static int test_audit_constants(void) {
    ASSERT(strcmp(FL_AUDIT_REL_DEFAULT, ".fl_audit.log") == 0);
    ASSERT(strcmp(FL_AUDIT_ENV, "FL_AUDIT") == 0);
    return 0;
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void) {
    if (make_tmpdir() != 0) {
        fprintf(stderr, "Failed to create temp directory.\n");
        return 1;
    }

    printf("test_audit_show_no_file... ");
    if (test_audit_show_no_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_empty_file... ");
    if (test_audit_show_empty_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_n_clamping... ");
    if (test_audit_show_n_clamping() != 0) return 1;
    printf("OK\n");

    printf("test_audit_show_returns_last_n... ");
    if (test_audit_show_returns_last_n() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_when_env_unset... ");
    if (test_audit_completed_noop_when_env_unset() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_env_zero... ");
    if (test_audit_completed_noop_env_zero() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_noop_null_cmd... ");
    if (test_audit_completed_noop_null_cmd() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_writes_file... ");
    if (test_audit_completed_writes_file() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_nonzero_exit_maps_to_err... ");
    if (test_audit_completed_nonzero_exit_maps_to_err() != 0) return 1;
    printf("OK\n");

    printf("test_audit_completed_sanitizes_control_chars... ");
    if (test_audit_completed_sanitizes_control_chars() != 0) return 1;
    printf("OK\n");

    printf("test_audit_sink_invoked... ");
    if (test_audit_sink_invoked() != 0) return 1;
    printf("OK\n");

    printf("test_audit_sink_null_safe... ");
    if (test_audit_sink_null_safe() != 0) return 1;
    printf("OK\n");

    printf("test_log_level_values... ");
    if (test_log_level_values() != 0) return 1;
    printf("OK\n");

    printf("test_authz_decisions... ");
    if (test_authz_decisions() != 0) return 1;
    printf("OK\n");

    printf("test_log_sink_ops_struct... ");
    if (test_log_sink_ops_struct() != 0) return 1;
    printf("OK\n");

    printf("test_log_dispatch_rate_limit... ");
    if (test_log_dispatch_rate_limit() != 0) return 1;
    printf("OK\n");

    printf("test_log_dispatch_null_sink... ");
    if (test_log_dispatch_null_sink() != 0) return 1;
    printf("OK\n");

    printf("test_audit_constants... ");
    if (test_audit_constants() != 0) return 1;
    printf("OK\n");

    rmrf_dir(g_tmpdir);
    printf("All audit log tests passed.\n");
    return 0;
}